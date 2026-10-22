// SPDX-License-Identifier: GPL-2.0-only
/* Recovered from M2391 mz_bl_lock.ko; keep the two-slot on-disk protocol. */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "../storage/meizu_storage.h"
#include "../system/meizu_sys.h"

#define LOCK_DATA_SIZE 1024
static DEFINE_MUTEX(mz_bl_lock_mutex);
static unsigned char lock_data[LOCK_DATA_SIZE];
static struct kobject *bl_lock_kobj;

static ssize_t show_bl_lock(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	static const int slots[] = { 68, 80 };
	bool locked = true;
	int i;

	/* Serialize reads too: the stock shared scratch buffer raced with stores. */
	mutex_lock(&mz_bl_lock_mutex);
	memset(lock_data, 0xff, sizeof(lock_data));
	for (i = 0; i < ARRAY_SIZE(slots); i++) {
		if (private_slot_rw(0, slots[i], lock_data, sizeof(lock_data)) < 0) {
			pr_err("read lock data failed\n");
			locked = false;
			break;
		}
		if (memchr_inv(lock_data, 0, sizeof(lock_data))) {
			pr_info("lock data not clear\n");
			locked = false;
			break;
		}
	}
	mutex_unlock(&mz_bl_lock_mutex);
	return sysfs_emit(buf, "%d\n", locked);
}

static ssize_t store_bl_lock(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	if (count < 4 || strncmp(buf, "lock", 4))
		return -EINVAL;

	mutex_lock(&mz_bl_lock_mutex);
	memset(lock_data, 0, sizeof(lock_data));
	/* Stock attempts both writes and acknowledges count even after I/O errors. */
	if (private_slot_rw(1, 68, lock_data, sizeof(lock_data)) < 0)
		pr_err("clear lock data failed\n");
	if (private_slot_rw(1, 80, lock_data, sizeof(lock_data)) < 0)
		pr_err("clear lock data failed\n");
	mutex_unlock(&mz_bl_lock_mutex);
	return count;
}

static struct kobj_attribute bl_lock_attr =
	__ATTR(bl_lock, 0644, show_bl_lock, store_bl_lock);

static int __init mz_bl_lock_init(void)
{
	int ret;

	bl_lock_kobj = meizu_sysfslink_get_kobj("bootloader");
	if (!bl_lock_kobj)
		return -ENOMEM;
	ret = sysfs_create_file(bl_lock_kobj, &bl_lock_attr.attr);
	if (ret) {
		kobject_put(bl_lock_kobj);
		bl_lock_kobj = NULL;
	}
	return ret;
}

static void __exit mz_bl_lock_exit(void)
{
	if (bl_lock_kobj) {
		sysfs_remove_file(bl_lock_kobj, &bl_lock_attr.attr);
		kobject_put(bl_lock_kobj);
	}
}
module_init(mz_bl_lock_init);
module_exit(mz_bl_lock_exit);
MODULE_DESCRIPTION("Meizu bootloader lock private-slot interface");
MODULE_LICENSE("GPL v2");
