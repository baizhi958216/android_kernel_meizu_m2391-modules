// SPDX-License-Identifier: GPL-2.0-only
/*
 * Recovered M2391 meizu_private.ko storage behavior (.text 0x2b4..0xae4).
 * Partition names/order from .data relocations: private,camera,rstinfo,param.
 * RSA/SHA verification is implemented separately in meizu_rsa.c.
 * All eight public export CRCs match stock with the current full headers.
 * The normal module build rechecks these interfaces before packaging.
 */
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>

#include "meizu_storage.h"

#define MZ_STORAGE_BLOCK_SIZE 4096
static BLOCKING_NOTIFIER_HEAD(meizu_storage_chain_head);
static DEFINE_MUTEX(storage_lock);
static struct {
	const char *name;
	struct block_device *bdev;
} partitions[] = {
	{ "private", NULL }, { "camera", NULL },
	{ "rstinfo", NULL }, { "param", NULL },
};

/* Caller holds storage_lock, also serializing partial-block updates. */
static struct block_device *partition_get(const char *name)
{
	char label[75];
	dev_t dev;
	struct block_device *bdev;
	unsigned int i;

	if (!name)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(partitions); i++)
		if (!strcmp(name, partitions[i].name))
			break;
	if (i == ARRAY_SIZE(partitions))
		return NULL;
	if (partitions[i].bdev)
		return partitions[i].bdev;
	if (snprintf(label, sizeof(label), "PARTLABEL=%s", name) >= sizeof(label))
		return NULL;
	dev = name_to_dev_t(label);
	if (!dev)
		return NULL;
	bdev = blkdev_get_by_dev(dev, FMODE_READ | FMODE_WRITE, NULL);
	if (IS_ERR(bdev))
		return NULL;
	partitions[i].bdev = bdev;
	return bdev;
}

static int storage_block_rw(struct block_device *bdev, int rw,
		sector_t block, size_t offset, void *buf, size_t len)
{
	struct buffer_head *bh;
	int ret = 0;

	ret = set_blocksize(bdev, MZ_STORAGE_BLOCK_SIZE);
	if (ret)
		return ret;
	bh = __getblk_gfp(bdev, block, MZ_STORAGE_BLOCK_SIZE, GFP_NOIO);
	if (!bh)
		return -ENOMEM;
	pm_runtime_get(&bdev->bd_device);
	/* Stock deliberately reads the whole block even for a complete write. */
	clear_buffer_uptodate(bh);
	lock_buffer(bh);
	get_bh(bh);
	bh->b_end_io = end_buffer_read_sync;
	submit_bh(REQ_OP_READ, REQ_SYNC, bh);
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		ret = -EIO;
		goto out;
	}
	if (rw) {
		lock_buffer(bh);
		memcpy(bh->b_data + offset, buf, len);
		get_bh(bh);
		bh->b_end_io = end_buffer_write_sync;
		submit_bh(REQ_OP_WRITE, REQ_SYNC, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			ret = -EIO;
	} else {
		memcpy(buf, bh->b_data + offset, len);
	}
out:
	pm_runtime_put(&bdev->bd_device);
	brelse(bh);
	return ret;
}

int sd_partition_rw(const char *part_name, int rw, loff_t offset,
		void *buf, size_t len)
{
	struct block_device *bdev;
	loff_t size;
	size_t chunk, within;
	int ret = 0;

	if (!buf || offset < 0 || (offset & 511))
		return -EINVAL;
	if (system_state == SYSTEM_POWER_OFF || system_state == SYSTEM_RESTART)
		return -EACCES;
	mutex_lock(&storage_lock);
	bdev = partition_get(part_name);
	if (!bdev) {
		ret = -ENODEV;
		goto out;
	}
	size = i_size_read(bdev->bd_inode);
	/* Subtraction avoids the original unchecked offset+len overflow. */
	if (offset > size || len > size - offset) {
		ret = -EINVAL;
		goto out;
	}
	while (len) {
		within = offset & (MZ_STORAGE_BLOCK_SIZE - 1);
		chunk = min_t(size_t, len, MZ_STORAGE_BLOCK_SIZE - within);
		ret = storage_block_rw(bdev, rw, offset / MZ_STORAGE_BLOCK_SIZE,
				within, buf, chunk);
		if (ret)
			break;
		offset += chunk;
		buf += chunk;
		len -= chunk;
	}
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
	ready = partition_get(part_name) != NULL;
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
		if (partitions[i].bdev)
			blkdev_put(partitions[i].bdev, FMODE_READ | FMODE_WRITE);
		partitions[i].bdev = NULL;
	}
	mutex_unlock(&storage_lock);
}
module_init(meizu_storage_init);
module_exit(meizu_storage_exit);
MODULE_IMPORT_NS(ANDROID_GKI_VFS_EXPORT_ONLY);
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Recovered Meizu M2391 storage and RSA verification APIs");
