// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "cs_m68.h"

static void cs_m68_free(struct kref *refcount)
{
	struct cs_m68 *cs = container_of(refcount, struct cs_m68, refcount);

	kfree(cs);
}

void cs_m68_put(struct cs_m68 *cs)
{
	kref_put(&cs->refcount, cs_m68_free);
}

static int cs_m68_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct cs_m68 *cs = container_of(misc, struct cs_m68, misc);
	int ret = 0;

	mutex_lock(&cs->access_lock);
	if (cs->removed)
		ret = -ENODEV;
	else {
		kref_get(&cs->refcount);
		file->private_data = cs;
	}
	mutex_unlock(&cs->access_lock);
	return ret;
}

static int cs_m68_close(struct inode *inode, struct file *file)
{
	cs_m68_put(file->private_data);
	return 0;
}

static ssize_t cs_m68_raw_read(struct file *file, char __user *buffer,
		size_t count, loff_t *position)
{
	struct cs_m68 *cs = file->private_data;
	u8 reg, *data;
	int ret;

	/* OEM ABI: count bytes follow a register byte at buffer[0]. */
	if (!count || count > U16_MAX)
		return -EINVAL;
	if (get_user(reg, buffer))
		return -EFAULT;
	data = kmalloc(count, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&cs->command_lock);
	ret = cs_m68_read(cs, reg, data, count);
	mutex_unlock(&cs->command_lock);
	if (ret >= 0 && copy_to_user(buffer + 1, data, count))
		ret = -EFAULT;
out:
	mutex_unlock(&cs->access_lock);
	kfree(data);
	/* Preserve stock's I2C message-count return, not a byte-count return. */
	return ret;
}

static ssize_t cs_m68_raw_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *position)
{
	struct cs_m68 *cs = file->private_data;
	u8 reg, *data;
	int ret;

	if (!count || count > U16_MAX - 1)
		return -EINVAL;
	if (get_user(reg, buffer))
		return -EFAULT;
	data = memdup_user(buffer + 1, count);
	if (IS_ERR(data))
		return PTR_ERR(data);
	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&cs->command_lock);
	ret = cs_m68_write(cs, reg, data, count);
	mutex_unlock(&cs->command_lock);
out:
	mutex_unlock(&cs->access_lock);
	kfree(data);
	return ret;
}

static long cs_m68_ioctl(struct file *file, unsigned int command,
		unsigned long argument)
{
	struct cs_m68 *cs = file->private_data;
	long ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	if (_IOC_TYPE(command) != 'n' || _IOC_NR(command) > 1) {
		ret = -ENOTTY;
		goto out;
	}
	if (_IOC_DIR(command) &&
		!access_ok((void __user *)argument, _IOC_SIZE(command))) {
		ret = -EFAULT;
		goto out;
	}
	/* These original compatibility ioctls perform no device transaction. */
	if (command == _IOW('n', 1, int))
		ret = 0;
	else if (command == _IOW('n', 0, int) && argument <= 2)
		ret = 0;
	else
		ret = -ENOTTY;
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static loff_t cs_m68_seek(struct file *file, loff_t offset, int whence)
{
	/* The stock g_cs_reg assignment is never consumed by read or write. */
	return offset;
}

const struct file_operations cs_m68_file_operations = {
	.owner = THIS_MODULE,
	.open = cs_m68_open,
	.release = cs_m68_close,
	.read = cs_m68_raw_read,
	.write = cs_m68_raw_write,
	.llseek = cs_m68_seek,
	.unlocked_ioctl = cs_m68_ioctl,
	.compat_ioctl = cs_m68_ioctl,
};
