// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 bootloader_log recovery: stock probe.text+0x1ac, show+0x444,
 * DT parser.init.text+0x34. No header, signature, length field or records:
 * the complete contiguous-region is simply a NUL-terminated text buffer.
 * Stock forced its final byte to NUL and printed "%s\n". This version bounds
 * reads to size-1 without modifying reserved RAM, preserving visible output.
 * The original noncached page mapping is retained with pfn validity/overflow
 * checks and proper of-node, page-array, proc-entry and mapping lifetimes.
 */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

struct bootloader_log_ram_zone {
	phys_addr_t paddr;
	size_t size;
	void *vaddr;
	char *buffer;
	struct proc_dir_entry *proc;
};
/* Original non-DT platform-data field order, resolved with stock split BTF. */
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
static int bootloader_log_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootloader_log_proc_show, PDE_DATA(inode));
}
static const struct proc_ops bootloader_log_proc_fops = {
	.proc_open = bootloader_log_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int bootloader_log_probe(struct platform_device *pdev)
{
	struct bootloader_log_ram_zone *zone;
	struct bootloader_log_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *memory;
	struct resource res;
	struct page **pages;
	phys_addr_t address;
	resource_size_t length;
	size_t offset, count, i, span;
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
	offset = offset_in_page(address);
	if (length > SIZE_MAX - offset - (PAGE_SIZE - 1))
		return -EOVERFLOW;
	span = length + offset;
	count = DIV_ROUND_UP(span, PAGE_SIZE);
	if (count > UINT_MAX || count > SIZE_MAX / sizeof(*pages))
		return -EOVERFLOW;
	zone = devm_kzalloc(&pdev->dev, sizeof(*zone), GFP_KERNEL);
	if (!zone)
		return -ENOMEM;
	pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	for (i = 0; i < count; i++) {
		unsigned long pfn = PHYS_PFN(address) + i;

		if (!pfn_valid(pfn)) {
			kfree(pages);
			return -EINVAL;
		}
		pages[i] = pfn_to_page(pfn);
	}
	zone->vaddr = vmap(pages, count, VM_MAP, pgprot_noncached(PAGE_KERNEL));
	kfree(pages);
	if (!zone->vaddr)
		return -ENOMEM;
	zone->paddr = address;
	zone->size = length;
	zone->buffer = (char *)zone->vaddr + offset;
	/* mode0 is stock; procfs supplies the regular-file read-only defaults. */
	zone->proc = proc_create_data("bootloader_log", 0, NULL,
			&bootloader_log_proc_fops, zone);
	if (!zone->proc) {
		vunmap(zone->vaddr);
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, zone);
	return 0;
}
static int bootloader_log_remove(struct platform_device *pdev)
{
	struct bootloader_log_ram_zone *zone = platform_get_drvdata(pdev);

	/* Remove/drain the reader callbacks before releasing the backing map. */
	proc_remove(zone->proc);
	vunmap(zone->vaddr);
	return 0;
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
MODULE_LICENSE("GPL");
