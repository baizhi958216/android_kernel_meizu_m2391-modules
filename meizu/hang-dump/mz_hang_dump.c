// SPDX-License-Identifier: GPL-2.0-only
/* M2391 original .text+0x1e8..0x99c and .init.text+0..0xbc.
 * All diagnostic and input behavior is implemented with public kernel APIs.
 * console_verbose/register_sysrq_key replace mzprobe's private address lookup.
 * Global key state is serialized across devices; unload drains input, irq_work
 * and SysRq readers. iterate_fd supplies the original per-fd DMA accounting
 * with the file-table lock held, instead of racing an unprotected fd array.
 */
#include <linux/cred.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/input.h>
#include <linux/irq_work.h>
#include <linux/ktime.h>
#include <linux/meizu_info.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/workqueue.h>

static DEFINE_SPINLOCK(pattern_lock);
static const unsigned int wdbite_pattern[] = {
	KEY_VOLUMEUP, KEY_VOLUMEUP, KEY_VOLUMEUP, KEY_VOLUMEUP, KEY_VOLUMEUP, KEY_VOLUMEUP,
	KEY_VOLUMEDOWN, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN,
};
static const unsigned int dump_pattern[] = {
	KEY_VOLUMEUP, KEY_VOLUMEUP, KEY_VOLUMEUP, KEY_VOLUMEUP,
	KEY_VOLUMEDOWN, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN, KEY_VOLUMEDOWN,
};
static unsigned int wdbite_index, dump_index, last_code;
static ktime_t last_time;
static struct irq_work mhd_irq_work;
static struct work_struct mhd_dump_work;
static bool pending_dump, pending_bite, pending_processes, pending_ion;
static bool stopping;
int mhd_dump_in_progress;

static const char * const task_state_array[] = {
	"R (running)", "S (sleeping)", "D (disk sleep)", "T (stopped)",
	"t (tracing stop)", "X (dead)", "Z (zombie)", "P (parked)", "I (idle)",
};
static const char *mhd_task_state(struct task_struct *task)
{
	unsigned int task_state = smp_load_acquire(&task->__state);
	unsigned int state = (task_state | READ_ONCE(task->exit_state)) & TASK_REPORT;

	if (task_state == TASK_IDLE)
		state = TASK_REPORT_IDLE;
	if (task_state == TASK_RTLOCK_WAIT)
		state = TASK_UNINTERRUPTIBLE;
	return task_state_array[fls(state)];
}

static int dump_processes(void)
{
	struct task_struct *task;
	int score_adj;

	pr_emerg("mhd: ============\tshow processes\t===========\n");
	pr_emerg("mhd: tsk_name                pid      ppid        uid        adj      score_adj      rss       rssfile      state\n");
	rcu_read_lock();
	for_each_process(task) {
		if (task->flags & PF_KTHREAD)
			continue;
		task_lock(task);
		if (!task->mm) {
			task_unlock(task);
			continue;
		}
		score_adj = READ_ONCE(task->signal->oom_score_adj);
		/* Original legacy column uses 18*score_adj/1000, without clamping. */
		pr_emerg("mhd: %-17s%10d%10d%10d%12d%12d%12ld%12lu%20s\n",
			task->comm, task->pid, task_ppid_nr(task),
			__kuid_val(task_uid(task)), score_adj * 18 / 1000, score_adj,
			get_mm_rss(task->mm), get_mm_counter(task->mm, MM_FILEPAGES),
			mhd_task_state(task));
		task_unlock(task);
	}
	rcu_read_unlock();
	return 0;
}

static int add_dma_buf_usage(const void *data, struct file *file, unsigned int fd)
{
	unsigned long *kilobytes = (unsigned long *)data;
	struct dma_buf *buffer;

	if (is_dma_buf_file(file)) {
		buffer = file->private_data;
		*kilobytes += buffer->size >> 10;
	}
	return 0;
}

static void defer_statistics(bool ion)
{
	unsigned long flags;

	spin_lock_irqsave(&pattern_lock, flags);
	if (!stopping) {
		if (ion)
			pending_ion = true;
		else
			pending_processes = true;
		irq_work_queue(&mhd_irq_work);
	}
	spin_unlock_irqrestore(&pattern_lock, flags);
}

static void sysrq_ion_usage_processes(int key)
{
	struct task_struct *task;
	unsigned long kilobytes;

	if (READ_ONCE(stopping))
		return;
	if (in_interrupt() || current_work() != &mhd_dump_work) {
		defer_statistics(true);
		return;
	}
	rcu_read_lock();
	for_each_process(task) {
		kilobytes = 0;
		task_lock(task);
		if (task->files)
			iterate_fd(task->files, 0, add_dma_buf_usage, &kilobytes);
		if (kilobytes)
			pr_info("mhd: %-17s(pid:%-6d) size: %14ldkB\n",
				task->comm, task->pid, kilobytes);
		task_unlock(task);
	}
	rcu_read_unlock();
}

static void sysrq_dump_processes(int key)
{
	if (READ_ONCE(stopping))
		return;
	if (in_interrupt() || current_work() != &mhd_dump_work)
		defer_statistics(false);
	else
		dump_processes();
}
static const struct sysrq_key_op dump_processes_op = {
	.handler = sysrq_dump_processes,
	.help_msg = "dump-processes(x)",
	.action_msg = "dump processes info",
	.enable_mask = 0x200,
};
static const struct sysrq_key_op ion_usage_op = {
	.handler = sysrq_ion_usage_processes,
	.help_msg = "print-ion-memory(a)",
	.action_msg = "Print ion Memory",
};

static void mhd_dump(struct work_struct *work)
{
	unsigned long flags;
	bool dump, bite, processes, ion;

	spin_lock_irqsave(&pattern_lock, flags);
	dump = pending_dump;
	bite = pending_bite;
	pending_dump = pending_bite = false;
	processes = pending_processes;
	ion = pending_ion;
	pending_processes = pending_ion = false;
	if (dump)
		dump_index = 0;
	if (bite)
		wdbite_index = 0;
	spin_unlock_irqrestore(&pattern_lock, flags);
	if (dump) {
		console_verbose();
		pr_info("mhd: start dump system state\n");
		WRITE_ONCE(mhd_dump_in_progress, 1);
		handle_sysrq('x');
		handle_sysrq('m');
		handle_sysrq('y');
		handle_sysrq('a');
		handle_sysrq('w');
		handle_sysrq('l');
		WRITE_ONCE(mhd_dump_in_progress, 0);
	}
	if (!dump && processes)
		dump_processes();
	if (!dump && ion)
		sysrq_ion_usage_processes('a');
	if (bite && !is_minidump())
		panic("trigger emergency system dump\n");
}

/* irq_work may interrupt a holder of task_lock/files->file_lock. Keep all
 * task/files inspection and SysRq execution in process context instead.
 */
static void mhd_queue_dump(struct irq_work *work)
{
	schedule_work(&mhd_dump_work);
}

static bool advance_pattern(unsigned int *index, const unsigned int *pattern,
		unsigned int length, unsigned int code, u64 delta)
{
	if (*index < length)
		*index = pattern[*index] == code ? *index + 1 : 0;
	else if (*index == length)
		++*index;
	else if (delta < 50000000 && code != last_code)
		return true;
	else
		*index = 0;
	return false;
}

static void mhd_events(struct input_handle *handle,
		const struct input_value *values, unsigned int count)
{
	unsigned long flags;
	unsigned int i, code;
	ktime_t now;
	u64 delta;
	bool trigger;

	for (i = 0; i < count; i++) {
		code = values[i].code;
		if (code == KEY_VOLUMEDOWN)
			pr_info("mhd: vol- key %s\n", values[i].value ? "down" : "up");
		else if (code == KEY_VOLUMEUP)
			pr_info("mhd: vol+ key %s\n", values[i].value ? "down" : "up");
		else if (code == KEY_POWER)
			pr_info("mhd: power key %s\n", values[i].value ? "down" : "up");
		if (values[i].type != EV_KEY || values[i].value != 1 ||
		    (code != KEY_VOLUMEUP && code != KEY_VOLUMEDOWN))
			continue;
		spin_lock_irqsave(&pattern_lock, flags);
		now = ktime_get();
		delta = (u64)ktime_to_ns(ktime_sub(now, last_time));
		trigger = false;
		if (delta > 500000000) {
			wdbite_index = code == wdbite_pattern[0];
			dump_index = code == dump_pattern[0];
		} else {
			trigger |= advance_pattern(&wdbite_index, wdbite_pattern,
				ARRAY_SIZE(wdbite_pattern), code, delta);
			trigger |= advance_pattern(&dump_index, dump_pattern,
				ARRAY_SIZE(dump_pattern), code, delta);
		}
		last_time = now;
		last_code = code;
		if (trigger) {
			/* Latch completed triggers before deferred scheduling, so a
			 * later key cannot erase a dump waiting for worker dispatch. */
			pending_dump |= dump_index >= ARRAY_SIZE(dump_pattern) + 1;
			pending_bite |= wdbite_index >= ARRAY_SIZE(wdbite_pattern) + 1;
			irq_work_queue(&mhd_irq_work);
		}
		spin_unlock_irqrestore(&pattern_lock, flags);
	}
}

static void mhd_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	struct input_value event = { .type = type, .code = code, .value = value };

	mhd_events(handle, &event, 1);
}

static int mhd_connect(struct input_handler *handler, struct input_dev *dev,
		const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "mhd";
	ret = input_register_handle(handle);
	if (ret)
		goto free_handle;
	ret = input_open_device(handle);
	if (!ret)
		return 0;
	input_unregister_handle(handle);
free_handle:
	kfree(handle);
	return ret;
}
static void mhd_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/* Original contains three identical EV_KEY entries with zero keybit masks:
 * it accepts any key-capable device rather than requiring all three buttons.
 */
#define MHD_INPUT_ID { .flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT, .evbit = { BIT_MASK(EV_KEY) } }
static const struct input_device_id mhd_ids[] = {
	MHD_INPUT_ID, MHD_INPUT_ID, MHD_INPUT_ID, { }
};
static struct input_handler mhd_handler = {
	.event = mhd_event,
	.events = mhd_events,
	.connect = mhd_connect,
	.disconnect = mhd_disconnect,
	.name = "mhd",
	.id_table = mhd_ids,
};

static int __init mz_hang_dump_init(void)
{
	int ret;

	INIT_WORK(&mhd_dump_work, mhd_dump);
	init_irq_work(&mhd_irq_work, mhd_queue_dump);
	ret = register_sysrq_key('x', &dump_processes_op);
	if (ret)
		return -EBUSY;
	ret = register_sysrq_key('a', &ion_usage_op);
	if (ret)
		goto unregister_x;
	ret = input_register_handler(&mhd_handler);
	if (!ret)
		return 0;
	unregister_sysrq_key('a', &ion_usage_op);
unregister_x:
	unregister_sysrq_key('x', &dump_processes_op);
	irq_work_sync(&mhd_irq_work);
	cancel_work_sync(&mhd_dump_work);
	return ret;
}

static void __exit mz_hang_dump_exit(void)
{
	unsigned long flags;

	input_unregister_handler(&mhd_handler);
	/* SysRq remains published until the end: block its deferred producers. */
	spin_lock_irqsave(&pattern_lock, flags);
	stopping = true;
	spin_unlock_irqrestore(&pattern_lock, flags);
	irq_work_sync(&mhd_irq_work);
	cancel_work_sync(&mhd_dump_work);
	unregister_sysrq_key('a', &ion_usage_op);
	unregister_sysrq_key('x', &dump_processes_op);
}
module_init(mz_hang_dump_init);
module_exit(mz_hang_dump_exit);
MODULE_AUTHOR("bsp@meizu.com>");
MODULE_IMPORT_NS(MINIDUMP);
MODULE_LICENSE("GPL");
