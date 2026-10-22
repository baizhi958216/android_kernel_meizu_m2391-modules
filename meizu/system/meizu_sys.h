/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_SYS_RECOVERED_H
#define _MEIZU_SYS_RECOVERED_H

#include <linux/device.h>
#include <linux/kobject.h>

/* Original structure tags, field names and types recovered from stock BTF. */
struct mz_sysfs_directory;
struct mz_sysfs_attr {
	struct kobj_attribute kattr;
	struct mz_sysfs_directory *parent;
};
struct mz_sysfs_directory {
	const char *dirname;
	struct kobject *dir;
	struct mz_sysfs_attr *attrs;
	size_t attrs_size;
	void *args;
};

int meizu_sysfslink_register(struct device *dev);
int meizu_sysfslink_register_name(struct device *dev, char *name);
int meizu_sysfslink_register_kobj_name(struct kobject *kobj, char *name);
int meizu_sysfslink_remove_name(char *name);
struct kobject *meizu_sysfslink_get_kobj(const char *name);
void meizu_sysfslink_unregister(struct device *dev);
void meizu_sysfslink_unregister_name(const char *name);
void meizu_sysfslink_unregister_kobj_name(const char *name);
int mz_sysfs_create_directory(struct mz_sysfs_directory *dir, void *data);
int mz_sysfs_create_directory_specify_parent(struct mz_sysfs_directory *dir,
		void *data, struct kobject *parent);
void mz_sysfs_destory_directory(struct mz_sysfs_directory *dir);
#endif
