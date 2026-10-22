// SPDX-License-Identifier: GPL-2.0-only
/*
 * Recovered from M2391 vendor_boot boot_mode.ko, 5.15.119.
 * get_boot_mode .text+0xbc reads the global word; init .init.text+0x30
 * reads /bootinfo:boot_mode, +0x78 finds /reserved-memory, +0x88 finds
 * mzversion_region, +0x98 translates its first resource, then +0xc8..0x330
 * clears and writes the platform/kernel/identity text to reserved RAM.
 *
 * This reconstruction retains that write, but replaces the hardcoded
 * linear-map alias and unchecked strcat with memremap and bounded writes.
 * Stock base BTF resolves getter type1777 to const unsigned int. Its
 * declaration reproduces stock CRC 8ff3f9d0 without any overrides.
 * The normal module build rechecks the interface before packaging.
 */
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include "boot_mode.h"

static u32 boot_mode;

const unsigned int get_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(get_boot_mode);

static size_t mz_append_property(char *buf, size_t size, size_t used,
		struct device_node *bootinfo, const char *label, const char *property)
{
	const char *value;

	if (of_property_read_string(bootinfo, property, &value))
		value = "NA";
	return used + scnprintf(buf + used, size - used, "\n%s: %s", label, value);
}

static void __init mz_version_init(struct device_node *bootinfo)
{
	struct device_node *reserved, *region;
	struct resource res;
	resource_size_t length;
	void *mapping;
	char *buf;
	size_t used, size;
	int ret;

	reserved = of_find_node_by_path("/reserved-memory");
	if (!reserved) {
		pr_err("boot_mode: missing /reserved-memory\n");
		return;
	}
	/* of_find_node_by_name consumes the reserved-node reference. */
	region = of_find_node_by_name(reserved, "mzversion_region");
	if (!region) {
		pr_err("boot_mode: missing mzversion_region\n");
		return;
	}
	ret = of_address_to_resource(region, 0, &res);
	of_node_put(region);
	if (ret) {
		pr_err("boot_mode: invalid mzversion_region resource: %d\n", ret);
		return;
	}
	if (res.end < res.start)
		return;
	length = resource_size(&res);
	if (!length || length > SIZE_MAX)
		return;
	size = length;
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return;
	used = scnprintf(buf, size, "PLATFORM: kalama\nLINUX_UTS_VERSION: %s\n",
			init_uts_ns.name.version);
	if (bootinfo) {
		/* First field follows the version newline without an extra newline. */
		const char *psn;

		if (of_property_read_string(bootinfo, "psn", &psn))
			psn = "NA";
		used += scnprintf(buf + used, size - used, "PSN: %s", psn);
		used = mz_append_property(buf, size, used, bootinfo, "SN", "sn");
		used = mz_append_property(buf, size, used, bootinfo, "IMEI1", "imei1");
		used = mz_append_property(buf, size, used, bootinfo, "IMEI2", "imei2");
		used = mz_append_property(buf, size, used, bootinfo, "MEID", "meid");
		scnprintf(buf + used, size - used, "\n");
	} else {
		scnprintf(buf + used, size - used,
				"Can't find /bootinfo node from FDT.\n");
	}

	mapping = memremap(res.start, size, MEMREMAP_WB);
	if (mapping) {
		/* Copy the complete zero-filled region, including the final NUL. */
		memcpy(mapping, buf, size);
		memunmap(mapping);
	} else {
		pr_err("boot_mode: cannot map mzversion_region\n");
	}
	kfree(buf);
}

static int __init mz_boot_mode_init(void)
{
	struct device_node *bootinfo = of_find_node_by_path("/bootinfo");

	/* Stock logs missing information and leaves the initial zero mode. */
	if (!bootinfo)
		pr_err("boot_mode: missing /bootinfo\n");
	else if (of_property_read_u32(bootinfo, "boot_mode", &boot_mode))
		pr_err("boot_mode: cannot read boot_mode property\n");
	mz_version_init(bootinfo);
	of_node_put(bootinfo);
	return 0;
}
module_init(mz_boot_mode_init);
/* Stock has no module exit handler: the reserved-memory record persists. */
MODULE_DESCRIPTION("M2391 boot mode and reserved-memory version record");
MODULE_LICENSE("GPL");
