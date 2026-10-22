/* SPDX-License-Identifier: GPL-2.0-only */
/* M2391's raw-register misc protocol: command 0 selects the read register,
 * command 1 writes consecutive registers. The ioctl has no command handlers
 * in the original driver; only its 'h' magic is checked.
 */
static void mz_haptic_release(struct kref *ref)
{
	struct mz_haptic_runtime *runtime = container_of(ref,
			struct mz_haptic_runtime, refcount);
	struct aw_haptic *aw = container_of(runtime, struct aw_haptic, meizu);

#ifdef AW_TIKTAP
	if (aw->start_buf) {
		ClearPageReserved(virt_to_page(aw->start_buf));
		free_pages((unsigned long)aw->start_buf, AW_TIKTAP_MMAP_PAGE_ORDER);
	}
#endif
	kfree(aw);
}

static int mz_haptic_file_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct mz_haptic_runtime *runtime = container_of(misc,
			struct mz_haptic_runtime, misc);

	/* misc_open and misc_deregister serialize publication through misc_mtx. */
	kref_get(&runtime->refcount);
	file->private_data = container_of(runtime, struct aw_haptic, meizu);
	return 0;
}

static int mz_haptic_file_release(struct inode *inode, struct file *file)
{
	struct aw_haptic *aw = file->private_data;

	kref_put(&aw->meizu.refcount, mz_haptic_release);
	return 0;
}

static ssize_t mz_haptic_file_read(struct file *file, char __user *user,
		size_t count, loff_t *position)
{
	struct aw_haptic *aw = file->private_data;
	u8 *data;
	size_t i;
	int ret = 0;

	if (!count)
		return 0;
	data = kmalloc(count, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	mutex_lock(&aw->lock);
	if (aw->meizu.removed) {
		ret = -ENODEV;
		goto out;
	}
	if (aw->meizu.file_command)
		goto out;
	for (i = 0; i < count; i++) {
		ret = haptic_hv_i2c_reads(aw, aw->meizu.file_register + i,
					&data[i], AW_I2C_BYTE_ONE);
		if (ret < 0)
			goto out;
	}
	ret = copy_to_user(user, data, count) ? -EFAULT : 0;
out:
	mutex_unlock(&aw->lock);
	kfree(data);
	return ret < 0 ? ret : count;
}

static ssize_t mz_haptic_file_write(struct file *file, const char __user *user,
		size_t count, loff_t *position)
{
	struct aw_haptic *aw = file->private_data;
	u8 *data;
	size_t i;
	int ret = 0;

	if (!count)
		return 0;
	data = memdup_user(user, count);
	if (IS_ERR(data))
		return PTR_ERR(data);
	mutex_lock(&aw->lock);
	if (aw->meizu.removed) {
		ret = -ENODEV;
		goto out;
	}
	aw->meizu.file_command = data[0];
	switch (data[0]) {
	case 0:
		if (count != 2) {
			ret = -EINVAL;
			break;
		}
		aw->meizu.file_register = data[1];
		break;
	case 1:
		if (count < 3) {
			ret = -EINVAL;
			break;
		}
		for (i = 2; i < count; i++) {
			ret = haptic_hv_i2c_writes(aw, data[1] + i - 2,
						 &data[i], AW_I2C_BYTE_ONE);
			if (ret < 0)
				break;
		}
		break;
	default:
		break;
	}
out:
	mutex_unlock(&aw->lock);
	kfree(data);
	return ret < 0 ? ret : count;
}

static long mz_haptic_file_ioctl(struct file *file, unsigned int command,
		unsigned long argument)
{
	return _IOC_TYPE(command) == 'h' ? 0 : -EINVAL;
}

static const struct file_operations mz_haptic_file_ops = {
	.owner = THIS_MODULE,
	.open = mz_haptic_file_open,
	.release = mz_haptic_file_release,
	.read = mz_haptic_file_read,
	.write = mz_haptic_file_write,
	.unlocked_ioctl = mz_haptic_file_ioctl,
	.llseek = no_llseek,
};
