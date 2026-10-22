// SPDX-License-Identifier: GPL-2.0-only
/* M2391 Cirrus calibration misc/sysfs protocol.  The six signed calibration
 * words are independent of the raw ASCII speaker-slot record.  No DSP command
 * is implemented by the shipping GET/SET/GET_CALIB ioctls; they return success.
 */
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "../storage/meizu_storage.h"

#define CRUS_COMMAND(nr) _IOC(_IOC_READ | _IOC_WRITE, 'a', nr, sizeof(void *))
#define CRUS_GET 219
#define CRUS_SET 220
#define CRUS_GET_CALIB 221
#define CRUS_SET_CALIB 222
#define CRUS_READ_SLOT 223
#define CRUS_WRITE_SLOT 224
struct crus_header {
	__u32 size;
	__u32 module_id;
	__u32 param_id;
	__u32 data_length;
	__u64 data;
};
struct crus_header32 {
	__u32 size;
	__u32 module_id;
	__u32 param_id;
	__u32 data_length;
	__u32 data;
};
static_assert(sizeof(struct crus_header) == 24);
static_assert(sizeof(struct crus_header32) == 20);
static DEFINE_MUTEX(calibration_lock);
static s32 calibration[6];

static int parse_calibration(const char *text, s32 values[6])
{
	return sscanf(text, "%d %d %d %d %d %d", &values[0], &values[1],
		&values[2], &values[3], &values[4], &values[5]) == 6 ? 0 : -EINVAL;
}
static int read_slot(char *text, size_t length)
{
	int ret;

	memset(text, 0, length);
	ret = private_slot_rw_speaker(0, text, length);
	text[length - 1] = '\0';
	return ret;
}
static ssize_t calibration_data_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	char slot[128];
	ssize_t ret;

	mutex_lock(&calibration_lock);
	ret = read_slot(slot, sizeof(slot));
	if (!ret)
		ret = sysfs_emit(buffer,
			"private slot: [%s]; this_ctrl.cal: [%d %d %d %d %d %d]\n",
			slot, calibration[0], calibration[1], calibration[2],
			calibration[3], calibration[4], calibration[5]);
	mutex_unlock(&calibration_lock);
	return ret;
}
static ssize_t calibration_data_store(struct device *dev,
		struct device_attribute *attr, const char *buffer, size_t count)
{
	char slot[128] = {};
	s32 values[6];
	int ret;

	if (count >= sizeof(slot))
		return -E2BIG;
	memcpy(slot, buffer, count);
	ret = parse_calibration(slot, values);
	if (ret)
		return ret;
	mutex_lock(&calibration_lock);
	/* One fully prepared write replaces the original erase-then-write:
	 * a failed second operation must not erase a previously valid record. */
	ret = private_slot_rw_speaker(1, slot, sizeof(slot));
	if (!ret)
		memcpy(calibration, values, sizeof(values));
	mutex_unlock(&calibration_lock);
	return ret ?: count;
}
static ssize_t resistance_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	char slot[128];
	s32 left, right;
	ssize_t ret;

	mutex_lock(&calibration_lock);
	ret = read_slot(slot, sizeof(slot));
	if (!ret) {
		/* Preserve the original 32-bit multiply/wrap then signed divide
		 * by 0xd56000; using a wider product would change the protocol. */
		left = (s32)((u32)calibration[2] * 10000U) / 13983744;
		right = (s32)((u32)calibration[5] * 10000U) / 13983744;
		ret = sysfs_emit(buffer, "private slot: [%s]; L/R resistance: [%d %d]\n",
			slot, left, right);
	}
	mutex_unlock(&calibration_lock);
	return ret;
}
static DEVICE_ATTR_RW(calibration_data);
static DEVICE_ATTR_RO(resistance);
static struct attribute *crus_attrs[] = {
	&dev_attr_resistance.attr, &dev_attr_calibration_data.attr, NULL,
};
ATTRIBUTE_GROUPS(crus);

static long crus_ioctl(unsigned int command, unsigned long argument, bool compat)
{
	struct crus_header header = {};
	void __user *arg = compat ? compat_ptr(argument) : (void __user *)argument;
	void __user *data;
	char slot[256] = {};
	s32 values[6];
	u32 size;
	unsigned int nr = _IOC_NR(command);
	int ret = 0;

	if (_IOC_TYPE(command) != 'a' || _IOC_DIR(command) != (_IOC_READ | _IOC_WRITE) ||
	    _IOC_SIZE(command) != (compat ? sizeof(compat_uptr_t) : sizeof(void *)) ||
	    nr < CRUS_GET || nr > CRUS_WRITE_SLOT)
		return -EINVAL;
	if (get_user(size, (u32 __user *)arg))
		return -EFAULT;
	/* Both original layouts were accepted by its zeroed 24-byte stack
	 * header. Reject oversized/truncated records before copying. */
	if (size != sizeof(header) && !(compat && size == sizeof(struct crus_header32)))
		return -EINVAL;
	if (copy_from_user(&header, arg, size))
		return -EFAULT;
	if (header.size != size)
		return -EINVAL;
	if (nr <= CRUS_GET_CALIB)
		return 0;
	if (header.data_length != sizeof(values))
		return -EINVAL;
	data = compat ? compat_ptr((u32)header.data) : u64_to_user_ptr(header.data);
	if (nr != CRUS_READ_SLOT && copy_from_user(values, data, sizeof(values)))
		return -EFAULT;
	mutex_lock(&calibration_lock);
	switch (nr) {
	case CRUS_SET_CALIB:
		memcpy(calibration, values, sizeof(values));
		break;
	case CRUS_READ_SLOT:
		ret = read_slot(slot, sizeof(slot));
		if (!ret)
			ret = parse_calibration(slot, values);
		break;
	case CRUS_WRITE_SLOT:
		scnprintf(slot, sizeof(slot), "%d %d %d %d %d %d",
			values[0], values[1], values[2], values[3], values[4], values[5]);
		ret = private_slot_rw_speaker(1, slot, sizeof(slot));
		if (!ret)
			memcpy(calibration, values, sizeof(values));
		break;
	}
	mutex_unlock(&calibration_lock);
	if (!ret && nr == CRUS_READ_SLOT && copy_to_user(data, values, sizeof(values)))
		ret = -EFAULT;
	return ret;
}
static long crus_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return crus_ioctl(cmd, arg, false);
}
#ifdef CONFIG_COMPAT
static long crus_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return crus_ioctl(cmd, arg, true);
}
#endif
static const struct file_operations crus_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = crus_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = crus_compat_ioctl,
#endif
	.llseek = no_llseek,
};
static struct miscdevice crus_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "msm_cirrus_playback",
	.fops = &crus_fops,
	.groups = crus_groups,
};
static int __init cirrus_cali_init(void)
{
	return misc_register(&crus_misc);
}
static void __exit cirrus_cali_exit(void)
{
	misc_deregister(&crus_misc);
}
module_init(cirrus_cali_init);
module_exit(cirrus_cali_exit);
MODULE_DESCRIPTION("M2391 Cirrus speaker calibration protocol");
MODULE_LICENSE("GPL");
