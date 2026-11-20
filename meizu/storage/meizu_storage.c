// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 storage partitions: private, camera, rstinfo and param.
 * RSA/SHA verification is implemented separately in meizu_rsa.c.
 */
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/reboot.h>

#include "meizu_storage.h"

#define MZ_STORAGE_BLOCK_SIZE 4096
static BLOCKING_NOTIFIER_HEAD(meizu_storage_chain_head);
static DEFINE_MUTEX(storage_lock);
static struct {
	const char *name;
	struct file *file;
} partitions[] = {
	{ "private", NULL }, { "camera", NULL },
	{ "rstinfo", NULL }, { "param", NULL },
};

/* Caller holds storage_lock, also serializing partial-block updates. */
static struct file *partition_get(const char *name)
{
	struct file *file;
	dev_t dev;
	unsigned int i;
	int ret;

	if (!name)
		return ERR_PTR(-EINVAL);
	for (i = 0; i < ARRAY_SIZE(partitions); i++)
		if (!strcmp(name, partitions[i].name))
			break;
	if (i == ARRAY_SIZE(partitions))
		return ERR_PTR(-ENODEV);
	if (partitions[i].file)
		return partitions[i].file;
	ret = bdev_lookup_by_partlabel(name, &dev);
	if (ret)
		return ERR_PTR(ret);
	file = bdev_file_open_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);
	if (!IS_ERR(file))
		partitions[i].file = file;
	return file;
}

static int storage_block_rw(struct file *file, bool write, loff_t base,
		size_t block_len, size_t offset, void *buf, size_t len, u8 *block)
{
	struct block_device *bdev = file_bdev(file);
	int ret;

	/*
	 * bdev_file_open_by_dev() supplies a driver-owned anonymous file, not
	 * a labelled devtmpfs inode. Submit partition I/O as a block driver;
	 * kernel_read/write would incorrectly apply the current HAL's file
	 * permissions to that anonymous inode. The caller holds storage_lock
	 * and has checked the fixed partition allowlist and all byte bounds.
	 */
	ret = bdev_rw_virt(bdev, base >> SECTOR_SHIFT, block, block_len, REQ_OP_READ);
	if (ret)
		return ret;
	if (!write) {
		memcpy(buf, block + offset, len);
		return 0;
	}
	if (bdev_read_only(bdev))
		return -EROFS;

	memcpy(block + offset, buf, len);
	ret = bdev_rw_virt(bdev, base >> SECTOR_SHIFT, block, block_len, REQ_OP_WRITE);
	if (ret)
		return ret;
	return blkdev_issue_flush(bdev);
}

int sd_partition_rw(const char *part_name, int rw, loff_t offset,
		void *buf, size_t len)
{
	u8 *block;
	struct file *file;
	loff_t size;
	int ret = 0;

	if (!buf || offset < 0 || (offset & 511))
		return -EINVAL;
	if (system_state == SYSTEM_POWER_OFF || system_state == SYSTEM_RESTART)
		return -EACCES;
	mutex_lock(&storage_lock);
	file = partition_get(part_name);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out;
	}
	size = bdev_nr_bytes(file_bdev(file));
	if (offset > size || len > size - offset) {
		ret = -EINVAL;
		goto out;
	}
	if (!len)
		goto out;
	block = kmalloc(MZ_STORAGE_BLOCK_SIZE, GFP_NOIO);
	if (!block) {
		ret = -ENOMEM;
		goto out;
	}
	while (len) {
		loff_t base = round_down(offset, MZ_STORAGE_BLOCK_SIZE);
		size_t block_len = min_t(loff_t, size - base, MZ_STORAGE_BLOCK_SIZE);
		size_t within = offset - base;
		size_t chunk = min(len, block_len - within);

		ret = storage_block_rw(file, rw, base, block_len, within, buf, chunk, block);
		if (ret)
			break;
		offset += chunk;
		buf += chunk;
		len -= chunk;
	}
	kfree_sensitive(block);
out:
	mutex_unlock(&storage_lock);
	return ret;
}
EXPORT_SYMBOL(sd_partition_rw);

int private_slot_rw(int rw, int slot, void *buf, size_t len)
{
	if ((unsigned int)slot > 1024)
		return -EINVAL;
	return sd_partition_rw("private", rw, (loff_t)slot << 10, buf, len);
}
EXPORT_SYMBOL(private_slot_rw);

int private_slot_rw_speaker(int rw, void *buf, size_t len)
{
	return sd_partition_rw("private", rw, 0x11c00, buf, len);
}
EXPORT_SYMBOL(private_slot_rw_speaker);

int param_slot_rw(int rw, int slot, void *buf, size_t len)
{
	if ((unsigned int)slot > 1024)
		return -EINVAL;
	/* The original ignores slot after validating it; do not invent an offset. */
	return sd_partition_rw("param", rw, 0, buf, len);
}
EXPORT_SYMBOL(param_slot_rw);

int sd_is_storage_ready(const char *part_name)
{
	int ready;

	mutex_lock(&storage_lock);
	ready = !IS_ERR(partition_get(part_name));
	mutex_unlock(&storage_lock);
	return ready;
}
EXPORT_SYMBOL(sd_is_storage_ready);

int sd_get_storage_block_size(void)
{
	return MZ_STORAGE_BLOCK_SIZE;
}
EXPORT_SYMBOL(sd_get_storage_block_size);

int register_meizu_storage_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&meizu_storage_chain_head, nb);
}
EXPORT_SYMBOL(register_meizu_storage_notifier);

static int __init meizu_storage_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(partitions); i++)
		if (sd_is_storage_ready(partitions[i].name))
			blocking_notifier_call_chain(&meizu_storage_chain_head, 1,
					(void *)partitions[i].name);
	return 0;
}

static void __exit meizu_storage_exit(void)
{
	unsigned int i;

	mutex_lock(&storage_lock);
	for (i = 0; i < ARRAY_SIZE(partitions); i++) {
		if (partitions[i].file)
			fput(partitions[i].file);
		partitions[i].file = NULL;
	}
	mutex_unlock(&storage_lock);
}
module_init(meizu_storage_init);
module_exit(meizu_storage_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Recovered Meizu M2391 storage and RSA verification APIs");
