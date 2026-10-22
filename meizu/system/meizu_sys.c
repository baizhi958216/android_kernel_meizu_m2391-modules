// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reconstructed from M2391 meizu-sys.ko (5.15.119).
 * Disassembly offsets: directory teardown 0x278, create-parent 0x308,
 * create 0x4a4, unregister-device 0x6b4, remove-name 0x6d8,
 * register-name 0x724, register-kobj 0x7b0, register 0x84c,
 * create-child-kobject 0x8cc.
 *
 * Recovered 64-bit directory layout: dirname +0, dir +8, attributes +16,
 * count +24, caller args +32. Attribute stride is 40 with owner at +32.
 * Stock base BTF recovered from vendor git 196dac4^:proprietary/boot/kernel
 * resolves all original local structure tags, member names and types.
 * All original consumers are rebuilt as one source ABI group. Eight complex
 * interfaces use natural current-header CRCs; retained binaries are forbidden
 * by the group gate. Three primitive signatures retain their original CRCs.
 * No CRC is overwritten. Original boundary layouts pass 129 BTF assertions.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include "meizu_sys.h"
#include "base.h"

static struct class *meizu_class;
static DEFINE_MUTEX(meizu_sysfslink_lock);

static struct kobject *meizu_root(void)
{
	return &meizu_class->p->subsys.kobj;
}

int meizu_sysfslink_register_name(struct device *dev, char *name)
{
	int ret;

	if (!dev)
		return -1;
	/* Stock uses dev->driver->name, not dev_name(dev). */
	ret = sysfs_create_link(meizu_root(), &dev->kobj,
			name ? name : dev->driver->name);
	if (ret < 0)
		pr_err("%s: cannot create sysfs link: %d\n", __func__, ret);
	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(meizu_sysfslink_register_name);

int meizu_sysfslink_register(struct device *dev)
{
	return meizu_sysfslink_register_name(dev, NULL);
}
EXPORT_SYMBOL(meizu_sysfslink_register);

int meizu_sysfslink_register_kobj_name(struct kobject *kobj, char *name)
{
	int ret;

	if (!kobj || !name)
		return -1;
	ret = sysfs_create_link(meizu_root(), kobj, name);
	if (ret < 0)
		pr_err("%s: cannot create sysfs link: %d\n", __func__, ret);
	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(meizu_sysfslink_register_kobj_name);

int meizu_sysfslink_remove_name(char *name)
{
	if (!name)
		return -EINVAL;
	if (meizu_class)
		sysfs_remove_link(meizu_root(), name);
	return 0;
}
EXPORT_SYMBOL(meizu_sysfslink_remove_name);

void meizu_sysfslink_unregister_name(const char *name)
{
	sysfs_remove_link(meizu_root(), name);
}
EXPORT_SYMBOL(meizu_sysfslink_unregister_name);

void meizu_sysfslink_unregister_kobj_name(const char *name)
{
	sysfs_remove_link(meizu_root(), name);
}
EXPORT_SYMBOL(meizu_sysfslink_unregister_kobj_name);

void meizu_sysfslink_unregister(struct device *dev)
{
	/* Despite its name, stock unregisters the device itself. */
	device_unregister(dev);
}
EXPORT_SYMBOL(meizu_sysfslink_unregister);

struct kobject *meizu_sysfslink_get_kobj(const char *name)
{
	struct kobject *kobj = NULL;

	/* Stock creates a new child, rather than finding an existing object. */
	mutex_lock(&meizu_sysfslink_lock);
	if (meizu_class)
		kobj = kobject_create_and_add(name, meizu_root());
	mutex_unlock(&meizu_sysfslink_lock);
	return kobj;
}
EXPORT_SYMBOL(meizu_sysfslink_get_kobj);

void mz_sysfs_destory_directory(struct mz_sysfs_directory *dir)
{
	size_t i;

	if (!dir || !dir->dir)
		return;
	for (i = 0; i < dir->attrs_size; i++) {
		struct mz_sysfs_attr *attr = &dir->attrs[i];

		if (attr->parent == dir) {
			sysfs_remove_file(dir->dir, &attr->kattr.attr);
			attr->parent = NULL;
		}
	}
	kobject_put(dir->dir);
	dir->dir = NULL;
}
EXPORT_SYMBOL(mz_sysfs_destory_directory);

int mz_sysfs_create_directory_specify_parent(struct mz_sysfs_directory *dir,
		void *data, struct kobject *parent)
{
	size_t i;
	int ret = 0;

	if (!dir || !dir->dirname || (dir->attrs_size && !dir->attrs))
		return -EINVAL;
	if (dir->dir)
		return -EINVAL;
	if (!meizu_class)
		return -ENOMEM;
	/* Do not steal an attribute from another live directory on rollback. */
	for (i = 0; i < dir->attrs_size; i++)
		if (dir->attrs[i].parent)
			return -EBUSY;
	dir->args = data;
	dir->dir = kobject_create_and_add(dir->dirname, parent);
	if (IS_ERR_OR_NULL(dir->dir)) {
		dir->dir = NULL;
		return -EINVAL;
	}

	for (i = 0; i < dir->attrs_size; i++) {
		struct mz_sysfs_attr *attr = &dir->attrs[i];

		/* Publish the owner before its callback becomes visible in sysfs. */
		attr->parent = dir;
		ret = sysfs_create_file(dir->dir, &attr->kattr.attr);
		if (ret < 0) {
			attr->parent = NULL;
			goto rollback;
		}
	}
	return 0;
rollback:
	mz_sysfs_destory_directory(dir);
	return ret;
}
EXPORT_SYMBOL(mz_sysfs_create_directory_specify_parent);

int mz_sysfs_create_directory(struct mz_sysfs_directory *dir, void *data)
{
	if (!dir)
		return -EINVAL;
	if (dir->dir)
		return -EINVAL;
	if (!meizu_class)
		return -ENOMEM;
	return mz_sysfs_create_directory_specify_parent(dir, data, meizu_root());
}
EXPORT_SYMBOL(mz_sysfs_create_directory);

static int __init meizu_sys_init(void)
{
	meizu_class = class_create(THIS_MODULE, "meizu");
	return IS_ERR(meizu_class) ? PTR_ERR(meizu_class) : 0;
}

static void __exit meizu_sys_exit(void)
{
	class_destroy(meizu_class);
	meizu_class = NULL;
}
module_init(meizu_sys_init);
module_exit(meizu_sys_exit);
MODULE_DESCRIPTION("M2391 Meizu sysfs class and directory helpers");
MODULE_LICENSE("GPL");
