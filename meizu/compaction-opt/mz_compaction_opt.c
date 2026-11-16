// SPDX-License-Identifier: GPL-2.0-only
/* Exclude HIGHATOMIC and CMA blocks from the proactive compaction score. */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <trace/hooks/mm.h>
#define CREATE_TRACE_POINTS
#include "compaction_opt_trace.h"

static bool opt_enabled = true;
static struct proc_dir_entry *compaction_dir;

static void fix_extrfrag_for_order(void *unused, struct zone *zone,
		unsigned int order, unsigned long *free_pages, unsigned long *suitable)
{
	unsigned long nr_free = 0, nr_high = 0, reserved = 0, high_reserved = 0;
	unsigned int o;

	if (!READ_ONCE(opt_enabled) || !zone->present_pages || order >= NR_PAGE_ORDERS)
		return;
	for (o = 0; o < NR_PAGE_ORDERS; o++) {
		struct free_area *area = &zone->free_area[o];
		struct list_head *cursor;
		unsigned long flags, blocks, excluded = 0;

		/* Sample the total and reserved lists together, one order at a
		 * time. A concurrent buddy split cannot underflow the result.
		 */
		spin_lock_irqsave(&zone->lock, flags);
		blocks = area->nr_free;
		list_for_each(cursor, &area->free_list[MIGRATE_HIGHATOMIC])
			excluded++;
#ifdef CONFIG_CMA
		list_for_each(cursor, &area->free_list[MIGRATE_CMA])
			excluded++;
#endif
		spin_unlock_irqrestore(&zone->lock, flags);

		nr_free += blocks << o;
		reserved += excluded << o;
		if (o >= order) {
			nr_high += blocks << (o - order);
			high_reserved += excluded << (o - order);
		}
	}
	trace_compaction_opt_fix_extrfrag(zone, nr_free, nr_high << order,
					  reserved, high_reserved << order, order);
	*free_pages = nr_free - reserved;
	*suitable = nr_high - high_reserved;
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
