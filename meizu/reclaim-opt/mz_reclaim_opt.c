// SPDX-License-Identifier: GPL-2.0-only
/* M2391 stock .text+0x14c..0x870, .init.text+0..0x258.
 * Preserve the two proc protocols and exact page thresholds. Full parsing and
 * a seqlock replace the original partially published six-value update; hook
 * unregister drains callbacks before module text is released.
 */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched/signal.h>
#include <linux/seqlock.h>
#include <linux/uaccess.h>
#include <linux/vmstat.h>
#include <trace/hooks/vmscan.h>

struct reclaim_configuration {
	int threshold[3];
	int swappiness[3];
};
static DEFINE_SEQLOCK(configuration_lock);
static struct reclaim_configuration configuration = {
	.threshold = { INT_MAX, INT_MAX, INT_MAX },
	.swappiness = { 40, 60, 80 },
};
static int direct_swappiness;
static struct proc_dir_entry *reclaim_dir;

static struct reclaim_configuration configuration_snapshot(void)
{
	struct reclaim_configuration result;
	unsigned int sequence;

	do {
		sequence = read_seqbegin(&configuration_lock);
		result = configuration;
	} while (read_seqretry(&configuration_lock, sequence));
	return result;
}

static void set_swappiness(void *unused, int *swappiness)
{
	struct reclaim_configuration config;
	unsigned long pages, file_pages;
	int i;

	if (!(current->flags & PF_KSWAPD)) {
		*swappiness = READ_ONCE(direct_swappiness);
		return;
	}
	pages = totalram_pages();
	if (pages <= 1572864 || pages >= 3145728) {
		*swappiness = 100;
		return;
	}
	/* global_node_page_state clamps each transient negative count to zero. */
	file_pages = global_node_page_state(NR_ACTIVE_FILE) +
		global_node_page_state(NR_INACTIVE_FILE);
	config = configuration_snapshot();
	for (i = 0; i < ARRAY_SIZE(config.threshold); i++)
		if (file_pages >= (unsigned long)(long)config.threshold[i]) {
			*swappiness = config.swappiness[i];
			return;
		}
	*swappiness = 150;
}

static ssize_t kswapd_read(struct file *file, char __user *buffer,
		size_t count, loff_t *offset)
{
	struct reclaim_configuration config = configuration_snapshot();
	unsigned long pages = totalram_pages();
	char text[300];
	int length, mb[3], i;

	if (pages <= 1572864 || pages >= 3145728)
		length = scnprintf(text, sizeof(text),
			"unsupport memory size, g_unsupported_kswapd_swappiness : %d\n", 100);
	else {
		/* ARM64 original computes wrapped 32-bit pages*4 / 1024. */
		for (i = 0; i < ARRAY_SIZE(mb); i++)
			mb[i] = (s32)((u32)config.threshold[i] << 2) / 1024;
		length = scnprintf(text, sizeof(text),
			"\n\n%s devices kswapd swappiness configuration:\n"
			"threshold\tswappiness\n%dMB        %d\n%dMB        %d\n%dMB        %d\n\n",
			pages <= 2097152 ? "8GB" : "12GB",
			mb[0], config.swappiness[0], mb[1], config.swappiness[1],
			mb[2], config.swappiness[2]);
	}
	return simple_read_from_buffer(buffer, count, offset, text, length);
}

static ssize_t kswapd_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *offset)
{
	struct reclaim_configuration updated;
	char text[200];
	size_t copied;
	unsigned long flags;
	int i;

	if (!count || count > sizeof(text))
		return -EINVAL;
	copied = min_t(size_t, count, sizeof(text) - 1);
	if (copy_from_user(text, buffer, copied))
		return -EFAULT;
	text[copied] = '\0';
	if (sscanf(strim(text), "%d %d %d %d %d %d",
		&updated.threshold[0], &updated.swappiness[0],
		&updated.threshold[1], &updated.swappiness[1],
		&updated.threshold[2], &updated.swappiness[2]) != 6)
		return -EINVAL;
	pr_info("mz_reclaim_opt: %d %d %d %d %d %d\n",
		updated.threshold[0], updated.swappiness[0],
		updated.threshold[1], updated.swappiness[1],
		updated.threshold[2], updated.swappiness[2]);
	/* Original LSL wN,#8 wraps; use unsigned arithmetic to avoid signed UB. */
	for (i = 0; i < ARRAY_SIZE(updated.threshold); i++)
		updated.threshold[i] = (s32)((u32)updated.threshold[i] << 8);
	write_seqlock_irqsave(&configuration_lock, flags);
	configuration = updated;
	write_sequnlock_irqrestore(&configuration_lock, flags);
	return copied;
}

static ssize_t direct_read(struct file *file, char __user *buffer,
		size_t count, loff_t *offset)
{
	char text[40];
	int length = scnprintf(text, sizeof(text), "g_direct_swappiness : %d\n",
		READ_ONCE(direct_swappiness));

	return simple_read_from_buffer(buffer, count, offset, text, length);
}

static ssize_t direct_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *offset)
{
	char text[20];
	size_t copied = min_t(size_t, count, sizeof(text) - 1);
	int value, ret;

	if (copy_from_user(text, buffer, copied))
		return -EFAULT;
	text[copied] = '\0';
	ret = kstrtoint(strim(text), 10, &value);
	if (ret)
		return ret;
	WRITE_ONCE(direct_swappiness, value);
	return copied;
}

static const struct proc_ops kswapd_ops = {
	.proc_read = kswapd_read,
	.proc_write = kswapd_write,
};
static const struct proc_ops direct_ops = {
	.proc_read = direct_read,
	.proc_write = direct_write,
};

static int __init mz_reclaim_opt_init(void)
{
	struct proc_dir_entry *memory_dir;
	struct task_struct *task;
	unsigned long pages = totalram_pages();
	int ret, pid = -1;

	if (pages > 1572864 && pages <= 2097152) {
		configuration.threshold[0] = 524288;
		configuration.threshold[1] = 262144;
		configuration.threshold[2] = 204800;
	} else if (pages > 2097152 && pages <= 3145728) {
		configuration.threshold[0] = 786432;
		configuration.threshold[1] = 460800;
		configuration.threshold[2] = 358400;
	}
	/* mz_memory is shared with other modules and has no module callbacks.
	 * proc_mkdir returns NULL for an existing directory: full-path fallback
	 * resolves that parent without taking ownership of somebody else's tree.
	 */
	memory_dir = proc_mkdir("mz_memory", NULL);
	reclaim_dir = memory_dir ? proc_mkdir("reclaim_opt", memory_dir) :
		proc_mkdir("mz_memory/reclaim_opt", NULL);
	if (!reclaim_dir ||
	    !proc_create("kswapd_reclaim_swappiness", 0666, reclaim_dir, &kswapd_ops) ||
	    !proc_create("direct_reclaim_swappiness", 0666, reclaim_dir, &direct_ops)) {
		ret = -ENOMEM;
		goto remove_proc;
	}
	ret = register_trace_android_vh_tune_swappiness(set_swappiness, NULL);
	if (ret)
		goto remove_proc;
	rcu_read_lock();
	for_each_process(task)
		if ((task->flags & PF_KTHREAD) && !strncmp(task->comm, "kswapd0", 7)) {
			pid = task->pid;
			break;
		}
	rcu_read_unlock();
	pr_info("mz_reclaim_opt: %s get kswapd pid %d!\n", __func__, pid);
	return 0;
remove_proc:
	proc_remove(reclaim_dir);
	reclaim_dir = NULL;
	return ret;
}

static void __exit mz_reclaim_opt_exit(void)
{
	unregister_trace_android_vh_tune_swappiness(set_swappiness, NULL);
	tracepoint_synchronize_unregister();
	proc_remove(reclaim_dir);
	pr_info("mz_reclaim_opt: %s succeed!\n", __func__);
}
module_init(mz_reclaim_opt_init);
module_exit(mz_reclaim_opt_exit);
MODULE_DESCRIPTION("Meizu Reclaim Optimization Driver");
MODULE_AUTHOR("<baihaowen@meizu.com>");
MODULE_LICENSE("GPL");
