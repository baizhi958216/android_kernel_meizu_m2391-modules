// SPDX-License-Identifier: GPL-2.0-only
/* Stock .text+0x65c..0x8a0 excludes HIGHATOMIC and CMA free lists from
 * fragmentation accounting. Exact 24-byte vmstat private argument recovered
 * from base BTF contig_page_info11523 and mm/vmstat.c's live hook caller.
 * No page or free list is modified. The diagnostic event's original fields,
 * calculations, spelling and proc protocol are preserved.
 */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <trace/hooks/mm.h>
#define CREATE_TRACE_POINTS
#include "compaction_opt_trace.h"

struct contig_page_info {
	unsigned long free_pages;
	unsigned long free_blocks_total;
	unsigned long free_blocks_suitable;
};
static bool opt_enabled = true;
static struct proc_dir_entry *compaction_dir;

static unsigned long count_migration_blocks(struct zone *zone,
		unsigned int order, int migration_type)
{
	struct list_head *head = &zone->free_area[order].free_list[migration_type];
	struct list_head *cursor;
	unsigned long flags, count = 0;

	spin_lock_irqsave(&zone->lock, flags);
	list_for_each(cursor, head)
		count++;
	spin_unlock_irqrestore(&zone->lock, flags);
	return count;
}

static void fix_extrfrag_for_order(void *unused, void *argument,
		struct zone *zone, unsigned int order)
{
	struct contig_page_info *info = argument;
	unsigned long all_trivial = 0, high_trivial = 0, blocks;
	unsigned int current_order;

	if (!info || !READ_ONCE(opt_enabled) || !zone->present_pages || order >= MAX_ORDER)
		return;
	for (current_order = 0; current_order < MAX_ORDER; current_order++) {
		if (!READ_ONCE(zone->free_area[current_order].nr_free))
			continue;
		blocks = count_migration_blocks(zone, current_order, MIGRATE_HIGHATOMIC);
#ifdef CONFIG_CMA
		blocks += count_migration_blocks(zone, current_order, MIGRATE_CMA);
#endif
		all_trivial += blocks << current_order;
		if (current_order >= order)
			high_trivial += blocks << (current_order - order);
	}
	trace_compaction_opt_fix_extrfrag(zone, info->free_pages,
		info->free_blocks_suitable << order, all_trivial,
		high_trivial << order, order);
	info->free_pages -= all_trivial;
	info->free_blocks_suitable -= high_trivial;
}

static ssize_t proc_opt_enabled_read(struct file *file, char __user *buffer,
		size_t count, loff_t *offset)
{
	char text[30];
	int length = scnprintf(text, sizeof(text), "%s\n",
		READ_ONCE(opt_enabled) ? "enabled" : "disabed");

	return simple_read_from_buffer(buffer, count, offset, text, length);
}

static ssize_t proc_opt_enabled_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *offset)
{
	char text[20] = { 0 };
	size_t copied = min_t(size_t, count, sizeof(text) - 1);
	bool enabled;

	if (copy_from_user(text, buffer, copied))
		return -EFAULT;
	/* Stock tests only byte zero: every input except initial '0' enables. */
	enabled = text[0] != '0';
	WRITE_ONCE(opt_enabled, enabled);
	pr_info("%s opt_enabled %d\n", __func__, enabled);
	return copied;
}
static const struct proc_ops proc_opt_enabled_fops = {
	.proc_read = proc_opt_enabled_read,
	.proc_write = proc_opt_enabled_write,
};

static int __init mz_compaction_opt_init(void)
{
	int ret;

	compaction_dir = proc_mkdir("mz_compaction_opt", NULL);
	if (!compaction_dir)
		return -ENOMEM;
	if (!proc_create("enabled", 0666, compaction_dir, &proc_opt_enabled_fops)) {
		ret = -ENOMEM;
		goto remove_proc;
	}
	ret = register_trace_android_vh_extfrag_for_order(fix_extrfrag_for_order, NULL);
	if (ret)
		goto remove_proc;
	return 0;
remove_proc:
	proc_remove(compaction_dir);
	return ret;
}

static void __exit mz_compaction_opt_exit(void)
{
	unregister_trace_android_vh_extfrag_for_order(fix_extrfrag_for_order, NULL);
	tracepoint_synchronize_unregister();
	proc_remove(compaction_dir);
}
module_init(mz_compaction_opt_init);
module_exit(mz_compaction_opt_exit);
MODULE_DESCRIPTION("Meizu Compaction Optimization Driver");
MODULE_AUTHOR("<chao.cheng@xjsd.com>");
MODULE_LICENSE("GPL");
