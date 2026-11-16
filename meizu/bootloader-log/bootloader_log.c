// SPDX-License-Identifier: GPL-2.0-only









#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

struct bootloader_log_ram_zone {
	size_t size;
	char *buffer;
	struct proc_dir_entry *proc;
};

struct bootloader_log_platform_data {
	unsigned long mem_size;
	unsigned long mem_address;
	unsigned int mem_type;
};

static int bootloader_log_proc_show(struct seq_file *seq, void *unused)
{
	struct bootloader_log_ram_zone *zone = seq->private;
	size_t i;

	for (i = 0; i < zone->size - 1; i++) {
		char c = READ_ONCE(zone->buffer[i]);

		if (!c)
			break;
		seq_putc(seq, c);
		if (seq_has_overflowed(seq))
			return 0;
	}
	seq_putc(seq, '\n');
	return 0;
}
static int bootloader_log_probe(struct platform_device *pdev)
{
	struct bootloader_log_ram_zone *zone;
	struct bootloader_log_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *memory;
	struct resource res;
	phys_addr_t address;
	resource_size_t length;
	int ret;

	if (pdev->dev.of_node) {
		memory = of_parse_phandle(pdev->dev.of_node, "linux,contiguous-region", 0);
		if (!memory)
			return -EINVAL;
		ret = of_address_to_resource(memory, 0, &res);
		of_node_put(memory);
		if (ret)
			return ret;
		if (res.end < res.start)
			return -EINVAL;
		address = res.start;
		length = resource_size(&res);
	} else if (pdata) {
		address = pdata->mem_address;
		length = pdata->mem_size;
	} else {
		return -EINVAL;
	}
	if (!length || length > SIZE_MAX)
		return -EINVAL;
	if (length - 1 > (phys_addr_t)~0 - address)
		return -EOVERFLOW;
	zone = devm_kzalloc(&pdev->dev, sizeof(*zone), GFP_KERNEL);
	if (!zone)
		return -ENOMEM;
	zone->buffer = devm_memremap(&pdev->dev, address, length, MEMREMAP_WB);
	if (IS_ERR(zone->buffer))
		return PTR_ERR(zone->buffer);
	zone->size = length;
	zone->proc = proc_create_single_data("bootloader_log", 0444, NULL,
					     bootloader_log_proc_show, zone);
	if (!zone->proc)
		return -ENOMEM;
	platform_set_drvdata(pdev, zone);
	return 0;
}
static void bootloader_log_remove(struct platform_device *pdev)
{
	struct bootloader_log_ram_zone *zone = platform_get_drvdata(pdev);

	/* Remove/drain the reader callbacks before releasing the backing map. */
	proc_remove(zone->proc);
}
static const struct of_device_id bootloader_log_of_match[] = {
	{ .compatible = "bootloader_log" },
	{ }
};
MODULE_DEVICE_TABLE(of, bootloader_log_of_match);
static struct platform_driver bootloader_log_driver = {
	.probe = bootloader_log_probe,
	.remove = bootloader_log_remove,
	.driver = { .name = "bootloader_log", .of_match_table = bootloader_log_of_match },
};
module_platform_driver(bootloader_log_driver);
MODULE_DESCRIPTION("Meizu ABL reserved-memory boot log");
MODULE_LICENSE("GPL");
