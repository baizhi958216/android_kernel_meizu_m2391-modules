// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of AI11BA Dtof_V2.1_Build20221202a.
 * Original protocols and evidence: m2391-recovery/stock-{lifecycle,userspace}.
 */
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/ipc_logging.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "dtof.h"
#include "../system/meizu_sys.h"

#define DTOF_VERSION "Dtof_V2.1_Build20221202a"
#define DTOF_READ_WORD 0x80044100U
#define DTOF_WRITE_WORD 0x80044101U
#define DTOF_RESET 0x4103U
#define DTOF_POWER_CONFIG 0x4107U
#define DTOF_GET_STATE 0x8008410fU
#define DTOF_READ_RAM 0x80104102U
#define DTOF_BATCH 0x8884410aU
#define DTOF_BENCH_READ 0x4114U
#define DTOF_RESET_HIGH 0x4115U

int s_module_load_state;
EXPORT_SYMBOL_GPL(s_module_load_state);
static DEFINE_MUTEX(dtof_registry_lock);
static struct dtof_chip *dtof_owner;

#define dtof_log(chip, fmt, ...) do { \
	if ((chip)->ipclog) \
		ipc_log_string((chip)->ipclog, "%s: " fmt, __func__, ##__VA_ARGS__); \
	dev_info(&(chip)->client->dev, "%s: " fmt, __func__, ##__VA_ARGS__); \
} while (0)

static void dtof_reset_level(struct dtof_chip *chip, bool high)
{
	gpio_set_value_cansleep(chip->reset_gpio, high);
	chip->reset_state = high;
	chip->reset_count++;
	if (high)
		usleep_range(1000, 2000);
	else
		usleep_range(75, 100);
}

static int write_reg_with_verify_unlock(struct dtof_chip *chip, u8 reg, u16 value)
{
	u16 actual;
	int i;

	for (i = 0; i < 10; ++i)
		if (!dtof_i2c_write_word_unlock(chip, reg, value) &&
		    !dtof_i2c_read_word_unlock(chip, reg, &actual) && actual == value)
			return 0;
	dtof_log(chip, "write reg verify fail, addr 0x%02x, val 0x%04x\n", reg, value);
	return -1;
}

static int dtof_reset(struct dtof_chip *chip, bool high)
{
	int rc;

	if (!high) {
		dtof_reset_level(chip, false);
		return 0;
	}
	rc = dtof_i2c_lock(chip);
	if (rc)
		return rc;
	dtof_reset_level(chip, true);
	rc = write_reg_with_verify_unlock(chip, 0xa2, 0x37d3);
	if (!rc) {
		dtof_log(chip, "a2 config ok\n");
		rc = write_reg_with_verify_unlock(chip, 0xd0, 0x0008);
	}
	dtof_i2c_unlock(chip);
	if (rc)
		return rc;
	dtof_log(chip, "d0 config ok\n");
	return dtof_i2c_write_word(chip, 0xb1, 0x8141);
}

static void dtof_free(struct kref *ref)
{
	struct dtof_chip *chip = container_of(ref, struct dtof_chip, ref);

	kfree(chip);
}

static int dtof_open(struct inode *inode, struct file *file)
{
	struct dtof_chip *chip = container_of(file->private_data,
		struct dtof_chip, misc);
	int rc = 0;

	mutex_lock(&chip->lock);
	if (chip->removing)
		rc = -ENODEV;
	else {
		kref_get(&chip->ref);
		file->private_data = chip;
	}
	mutex_unlock(&chip->lock);
	if (rc)
		return rc;
	return nonseekable_open(inode, file);
}

static int dtof_release(struct inode *inode, struct file *file)
{
	struct dtof_chip *chip = file->private_data;

	file->private_data = NULL;
	kref_put(&chip->ref, dtof_free);
	return 0;
}

static long dtof_ioctl_locked(struct dtof_chip *chip, unsigned int cmd,
		void __user *arg)
{
	struct { u8 reg, reserved; u16 value; } word;
	struct { u16 count, offset; u32 reserved; u64 ptr; } ram;
	u32 state[2], value;
	struct dtof_rw_reg_batch_s *batch;
	u16 *words, data;
	int rc, i;
	ktime_t start;

	switch (cmd) {
	case DTOF_READ_WORD:
	case DTOF_WRITE_WORD:
		if (copy_from_user(&word, arg, sizeof(word)))
			return -EFAULT;
		if (cmd == DTOF_WRITE_WORD)
			return dtof_i2c_write_word(chip, word.reg, word.value);
		rc = dtof_i2c_read_word(chip, word.reg, &word.value);
		if (rc)
			return rc;
		return copy_to_user(arg, &word, sizeof(word)) ? -EFAULT : 0;
	case DTOF_RESET:
		if (copy_from_user(&value, arg, sizeof(value)))
			return -EFAULT;
		return dtof_reset(chip, !!value);
	case DTOF_POWER_CONFIG:
		/* Stock compiles out regulator control: it only accepts the value. */
		return copy_from_user(&value, arg, sizeof(value)) ? -EFAULT : 0;
	case DTOF_GET_STATE:
		state[0] = chip->reset_state;
		state[1] = chip->reset_count;
		return copy_to_user(arg, state, sizeof(state)) ? -EFAULT : 0;
	case DTOF_READ_RAM:
		if (copy_from_user(&ram, arg, sizeof(ram)))
			return -EFAULT;
		if (ram.count > U16_MAX / 2)
			return -EINVAL;
		words = kmalloc_array(ram.count, sizeof(*words), GFP_KERNEL);
		if (!words)
			return -ENOMEM;
		rc = dtof_i2c_read_ram_burst(chip, words, ram.offset, ram.count);
		if (!rc && copy_to_user(u64_to_user_ptr(ram.ptr), words,
				(size_t)ram.count * sizeof(*words)))
			rc = -EFAULT;
		kfree(words);
		return rc;
	case DTOF_BATCH:
		batch = memdup_user(arg, sizeof(*batch));
		if (IS_ERR(batch))
			return PTR_ERR(batch);
		rc = dtof_i2c_rw_batch(chip, batch);
		if (!rc && copy_to_user(arg, batch, sizeof(*batch)))
			rc = -EFAULT;
		kfree(batch);
		return rc;
	case DTOF_BENCH_READ:
		start = ktime_get();
		dtof_log(chip, "start debug 10\n");
		for (i = 0; i < 10000; ++i)
			dtof_i2c_read_word(chip, 0, &data);
		dtof_log(chip, "cnt=0 difftime=%lld(ns)\n",
			ktime_to_ns(ktime_sub(ktime_get(), start)));
		return 0;
	case DTOF_RESET_HIGH:
		/* Original factory operation raises XSDN without writing registers. */
		dtof_reset_level(chip, true);
		dtof_log(chip, "dtof reset high\n");
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}

static long dtof_unlocked_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct dtof_chip *chip = file->private_data;
	long rc;

	mutex_lock(&chip->lock);
	rc = chip->removing ? -ENODEV :
		dtof_ioctl_locked(chip, cmd, (void __user *)arg);
	mutex_unlock(&chip->lock);
	return rc;
}

static const struct file_operations dtof_fops = {
	.owner = THIS_MODULE,
	.open = dtof_open,
	.release = dtof_release,
	.unlocked_ioctl = dtof_unlocked_ioctl,
	.llseek = no_llseek,
};

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", DTOF_VERSION);
}
static DEVICE_ATTR_RO(version);

static ssize_t care_gpio_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct dtof_chip *chip = dev_get_drvdata(dev);
	struct gpio_desc *desc;
	ssize_t rc;
	int dir, value;

	mutex_lock(&chip->lock);
	desc = gpio_to_desc(chip->care_gpio + 301);
	if (chip->removing || !desc) {
		rc = -ENODEV;
		goto out;
	}
	value = gpiod_get_raw_value_cansleep(desc);
	dir = gpiod_get_direction(desc);
	rc = scnprintf(buf, PAGE_SIZE,
		"care_gpio:%4d Direction:%s Value:%d activeLow:%s\r\n",
		chip->care_gpio, dir < 0 ? "Err" : dir ? "In" : "Out", value,
		gpiod_is_active_low(desc) ? "Yes" : "No");
out:
	mutex_unlock(&chip->lock);
	return rc;
}

static ssize_t care_gpio_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct dtof_chip *chip = dev_get_drvdata(dev);
	struct gpio_desc *desc;
	int gpio, value, n, rc = 0;

	n = sscanf(buf, "%d %d", &gpio, &value);
	if (n < 1 || gpio < 0 || gpio > INT_MAX - 301)
		return -EINVAL;
	mutex_lock(&chip->lock);
	if (chip->removing) {
		rc = -ENODEV;
		goto out;
	}
	desc = gpio_to_desc(gpio + 301);
	if (!desc) {
		rc = -EINVAL;
		goto out;
	}
	chip->care_gpio = gpio;
	if (n == 2) {
		rc = gpiod_direction_output_raw(desc, 2);
		if (!rc)
			gpiod_set_raw_value_cansleep(desc, value);
	}
out:
	mutex_unlock(&chip->lock);
	return rc ? rc : count;
}
static DEVICE_ATTR_RW(care_gpio);

static struct attribute *dtof_attrs[] = {
	&dev_attr_version.attr, &dev_attr_care_gpio.attr, NULL,
};
static const struct attribute_group dtof_group = { .attrs = dtof_attrs };

static int dtof_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct dtof_chip *chip;
	u16 chip_id = 0;
	int rc, i;

	mutex_lock(&dtof_registry_lock);
	if (dtof_owner) {
		mutex_unlock(&dtof_registry_lock);
		return -EBUSY;
	}
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		mutex_unlock(&dtof_registry_lock);
		return -ENOMEM;
	}
	chip->client = client;
	mutex_init(&chip->lock);
	kref_init(&chip->ref);
	chip->reset_gpio = of_get_named_gpio(client->dev.of_node, "reset-gpio", 0);
	if (chip->reset_gpio < 0) {
		rc = chip->reset_gpio;
		goto free_chip;
	}
	chip->ipclog = ipc_log_context_create(10, "dtof_only", 0);
	dtof_log(chip, "dtof_i2c_probe enter ! driver ver: %s\n", DTOF_VERSION);
	/* Original AI11BA build explicitly disables regulator/poweroff control. */
	rc = gpio_request(chip->reset_gpio, "reset-gpio");
	if (rc)
		goto destroy_log;
	rc = gpio_direction_output(chip->reset_gpio, 0);
	if (rc)
		goto free_gpio;
	dtof_reset_level(chip, false);
	dtof_reset(chip, true);
	for (i = 0; i < 5; ++i) {
		rc = dtof_i2c_read_word(chip, 0, &chip_id);
		if (!rc && (chip_id == 0xa118 || chip_id == 0xa11b))
			break;
		if (!rc) {
			dtof_reset_level(chip, false);
			dtof_reset(chip, true);
		}
	}
	if (i == 5) {
		dtof_reset_level(chip, false);
		rc = -EIO;
		goto failed_probe;
	}
	dtof_reset_level(chip, false);
	chip->misc.minor = MISC_DYNAMIC_MINOR;
	chip->misc.name = "dtof";
	chip->misc.fops = &dtof_fops;
	chip->misc.parent = &client->dev;
	i2c_set_clientdata(client, chip);
	rc = misc_register(&chip->misc);
	if (rc)
		goto failed_probe;
	rc = sysfs_create_group(&client->dev.kobj, &dtof_group);
	if (rc)
		goto remove_misc;
	rc = meizu_sysfslink_register_name(&client->dev, "dtof");
	if (rc)
		goto remove_group;
	chip->link_created = true;
	dtof_owner = chip;
	WRITE_ONCE(s_module_load_state, 1);
	mutex_unlock(&dtof_registry_lock);
	return 0;
remove_group:
	sysfs_remove_group(&client->dev.kobj, &dtof_group);
remove_misc:
	mutex_lock(&chip->lock);
	chip->removing = true;
	mutex_unlock(&chip->lock);
	misc_deregister(&chip->misc);
failed_probe:
	WRITE_ONCE(s_module_load_state, 2);
free_gpio:
	gpio_free(chip->reset_gpio);
destroy_log:
	if (chip->ipclog)
		ipc_log_context_destroy(chip->ipclog);
free_chip:
	kref_put(&chip->ref, dtof_free);
	mutex_unlock(&dtof_registry_lock);
	return rc;
}

static int dtof_i2c_remove(struct i2c_client *client)
{
	struct dtof_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	chip->removing = true;
	mutex_unlock(&chip->lock);
	sysfs_remove_group(&client->dev.kobj, &dtof_group);
	if (chip->link_created)
		meizu_sysfslink_remove_name("dtof");
	misc_deregister(&chip->misc);
	mutex_lock(&dtof_registry_lock);
	dtof_owner = NULL;
	WRITE_ONCE(s_module_load_state, 2);
	gpio_free(chip->reset_gpio);
	if (chip->ipclog)
		ipc_log_context_destroy(chip->ipclog);
	mutex_unlock(&dtof_registry_lock);
	kref_put(&chip->ref, dtof_free);
	return 0;
}

static const struct of_device_id dtof_of_match[] = {
	{ .compatible = "dtof,ai11ba_i2c" }, { },
};
MODULE_DEVICE_TABLE(of, dtof_of_match);
static const struct i2c_device_id dtof_id[] = { { "dtof", 0 }, { } };
MODULE_DEVICE_TABLE(i2c, dtof_id);
static struct i2c_driver dtof_i2c_driver = {
	.driver = { .name = "dtof", .of_match_table = dtof_of_match },
	.probe = dtof_i2c_probe,
	.remove = dtof_i2c_remove,
	.id_table = dtof_id,
};
module_i2c_driver(dtof_i2c_driver);
MODULE_DESCRIPTION("M2391 AI11BA dToF driver");
MODULE_LICENSE("GPL v2");
