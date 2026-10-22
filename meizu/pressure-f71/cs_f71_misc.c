// SPDX-License-Identifier: GPL-2.0-only
/* Original /dev/ndt framing: count is payload length, buffer[0] is register,
 * read/write payload starts at buffer+1; success returns 1, not count.
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "cs_f71.h"

static void cs_f71_release_state(struct kref *ref)
{
	kfree(container_of(ref, struct cs_f71, ref));
}

void cs_f71_put(struct cs_f71 *cs)
{
	kref_put(&cs->ref, cs_f71_release_state);
}

static int cs_f71_misc_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct cs_f71 *cs = container_of(misc, struct cs_f71, misc);
	int ret = 0;

	/* misc_open invokes this under misc_mtx; misc_deregister drains that
	 * publication lock before the driver drops its owning reference.
	 */
	mutex_lock(&cs->command_lock);
	if (cs->removed)
		ret = -ENODEV;
	else {
		kref_get(&cs->ref);
		file->private_data = cs;
	}
	mutex_unlock(&cs->command_lock);
	return ret;
}

static int cs_f71_misc_release(struct inode *inode, struct file *file)
{
	cs_f71_put(file->private_data);
	return 0;
}

static ssize_t cs_f71_misc_read(struct file *file, char __user *user,
		size_t count, loff_t *offset)
{
	struct cs_f71 *cs = file->private_data;
	u8 reg, *bytes;
	int ret;

	if (!count || count > U16_MAX)
		return -EINVAL;
	if (copy_from_user(&reg, user, 1))
		return -EFAULT;
	bytes = kmalloc(count, GFP_KERNEL);
	if (!bytes)
		return -ENOMEM;
	mutex_lock(&cs->command_lock);
	ret = cs->removed ? -ENODEV : cs_f71_read(cs, reg, bytes, count);
	mutex_unlock(&cs->command_lock);
	if (!ret)
		ret = copy_to_user(user + 1, bytes, count) ? -EFAULT : 1;
	kfree(bytes);
	return ret;
}

static ssize_t cs_f71_misc_write(struct file *file, const char __user *user,
		size_t count, loff_t *offset)
{
	struct cs_f71 *cs = file->private_data;
	u8 reg, *bytes;
	int ret;

	/* Original low8 length truncation must not turn oversized writes into
	 * an unrelated register transaction.
	 */
	if (!count || count > U8_MAX)
		return -EINVAL;
	if (copy_from_user(&reg, user, 1))
		return -EFAULT;
	bytes = memdup_user(user + 1, count);
	if (IS_ERR(bytes))
		return PTR_ERR(bytes);
	mutex_lock(&cs->command_lock);
	ret = cs->removed ? -ENODEV : cs_f71_write(cs, reg, bytes, count);
	mutex_unlock(&cs->command_lock);
	kfree(bytes);
	return ret ? ret : 1;
}

static const struct file_operations cs_f71_fops = {
	.owner = THIS_MODULE,
	.open = cs_f71_misc_open,
	.release = cs_f71_misc_release,
	.read = cs_f71_misc_read,
	.write = cs_f71_misc_write,
	.llseek = no_llseek,
};

int cs_f71_misc_init(struct cs_f71 *cs)
{
	cs->misc.minor = MISC_DYNAMIC_MINOR;
	cs->misc.name = "ndt";
	cs->misc.fops = &cs_f71_fops;
	cs->misc.parent = &cs->client->dev;
	return misc_register(&cs->misc);
}

void cs_f71_misc_exit(struct cs_f71 *cs)
{
	misc_deregister(&cs->misc);
}
