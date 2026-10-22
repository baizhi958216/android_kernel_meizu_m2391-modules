// SPDX-License-Identifier: GPL-2.0-only
/* Original M2391 QoS diagnostics: source attribution, four-event per-request
 * history, a replayable 512-event stream, proc snapshots and clock trace events.
 * Contexts are privately indexed and referenced, rather than trusting arbitrary
 * request OEM pointers or retaining pointers into a removed cpufreq policy.
 */
#include <linux/cpufreq.h>
#include <linux/hashtable.h>
#include <linux/kallsyms.h>
#include <linux/kref.h>
#include <linux/meizu_freq_qos.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <trace/events/power.h>
#include <trace/hooks/power.h>
#include "mz_freq_qos_strings.h"

#define HISTORY_COUNT 4
#define STREAM_COUNT 512
struct qos_context;
struct freq_qos_record {
	pid_t pid;
	pid_t tgid;
	char comm[20];
	int freq;
	ktime_t kt;
	struct qos_context *oem;
};
static_assert(sizeof(struct freq_qos_record) == 48);
static_assert(offsetof(struct freq_qos_record, freq) == 28);
static_assert(offsetof(struct freq_qos_record, kt) == 32);
static_assert(offsetof(struct freq_qos_record, oem) == 40);
struct qos_context {
	struct hlist_node node;
	struct kref refs;
	const struct freq_qos_request *request;
	unsigned int cpu;
	unsigned int min_freq;
	unsigned int max_freq;
	enum freq_qos_req_type type;
	char source[KSYM_SYMBOL_LEN];
	struct freq_qos_record history[HISTORY_COUNT];
	u64 total;
};
struct log_reader {
	struct mutex lock;
	u64 cursor;
	char text[128];
	size_t length;
	size_t offset;
};
static DEFINE_HASHTABLE(contexts, 6);
static DEFINE_SPINLOCK(monitor_lock);
static DECLARE_WAIT_QUEUE_HEAD(stream_wait);
static struct freq_qos_record stream[STREAM_COUNT];
static u64 stream_total;
static atomic_t log_opened = ATOMIC_INIT(0);
static struct proc_dir_entry *proc_dir;
static bool stopping;
static bool systrace_en;

static void context_release(struct kref *ref)
{
	kfree(container_of(ref, struct qos_context, refs));
}
static void context_put(struct qos_context *ctx)
{
	if (ctx)
		kref_put(&ctx->refs, context_release);
}
/* monitor_lock protects both the index and the owning request reference. */
static struct qos_context *find_context(const struct freq_qos_request *req)
{
	struct qos_context *ctx;

	hash_for_each_possible(contexts, ctx, node, (unsigned long)req)
		if (ctx->request == req)
			return ctx;
	return NULL;
}
static bool ignored_source(unsigned long first, unsigned long second)
{
	char name[KSYM_SYMBOL_LEN];
	unsigned long addresses[] = { first, second };
	int i;

	for (i = 0; i < ARRAY_SIZE(addresses); i++) {
		scnprintf(name, sizeof(name), "%pS", (void *)addresses[i]);
		if (strstr(name, "dev_pm_qos") || strstr(name, "_devfreq_") ||
		    strstr(name, "_test_"))
			return true;
	}
	return false;
}
static void source_name(char *dest, unsigned long address, bool addition)
{
	const struct source_mapping *map = addition ? add_sources : update_sources;
	size_t count = addition ? ARRAY_SIZE(add_sources) : ARRAY_SIZE(update_sources);
	char name[KSYM_SYMBOL_LEN];
	size_t i;

	sprint_symbol_no_offset(name, address);
	for (i = 0; i < count; i++) {
		if (strstr(name, map[i].symbol)) {
			strscpy(dest, map[i].module, KSYM_SYMBOL_LEN);
			return;
		}
	}
	scnprintf(dest, KSYM_SYMBOL_LEN, "raw- %s", name);
}
static struct qos_context *new_context(const struct freq_qos_request *req,
		unsigned long caller, bool addition)
{
	struct cpufreq_policy *policy;
	struct qos_context *ctx = NULL;
	int cpu;

	if (!READ_ONCE(systrace_en) || !req->qos)
		return NULL;
	/* Unlike the old container_of(qos, policy, constraints), validate that
	 * this really is an active cpufreq policy before reading its fields.
	 */
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (&policy->constraints == req->qos) {
			ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
			if (ctx) {
				kref_init(&ctx->refs);
				ctx->request = req;
				ctx->cpu = policy->cpu;
				ctx->min_freq = policy->cpuinfo.min_freq;
				ctx->max_freq = policy->cpuinfo.max_freq;
				ctx->type = req->type;
				source_name(ctx->source, caller, addition);
			}
			cpufreq_cpu_put(policy);
			break;
		}
		cpufreq_cpu_put(policy);
	}
	return ctx;
}
static void trace_request(struct qos_context *ctx, const char *operation,
			  int value)
{
	char name[256];
	unsigned int frequency;

	if (!READ_ONCE(systrace_en))
		return;
	frequency = value < 0 || operation[0] == 'R' ? 0 : clamp_t(unsigned int, value,
					 ctx->min_freq, ctx->max_freq);
	scnprintf(name, sizeof(name), "policy-%u,%s-%s-%s\n", ctx->cpu,
		operation, ctx->type == FREQ_QOS_MIN ? "min" : "max", ctx->source);
	trace_clock_set_rate(name, frequency, ctx->cpu);
}
static void record_request(struct freq_qos_request *req, int value,
		unsigned long first, unsigned long second, bool addition)
{
	struct freq_qos_record event = {};
	struct qos_context *ctx, *candidate = NULL, *old;
	unsigned long flags;

	if (!req || !req->qos || ignored_source(first, second))
		return;
	spin_lock_irqsave(&monitor_lock, flags);
	ctx = find_context(req);
	if (ctx)
		kref_get(&ctx->refs);
	spin_unlock_irqrestore(&monitor_lock, flags);
	if (!ctx) {
		candidate = new_context(req, second, addition);
		if (!candidate)
			return;
		spin_lock_irqsave(&monitor_lock, flags);
		ctx = find_context(req);
		if (!ctx && !READ_ONCE(stopping)) {
			ctx = candidate;
			candidate = NULL;
			hash_add(contexts, &ctx->node, (unsigned long)req);
		}
		if (ctx)
			kref_get(&ctx->refs);
		spin_unlock_irqrestore(&monitor_lock, flags);
		context_put(candidate);
		if (!ctx)
			return;
	}
	event.pid = task_pid_nr(current);
	event.tgid = task_tgid_nr(current);
	{
		char comm[TASK_COMM_LEN];

		get_task_comm(comm, current);
		memcpy(event.comm, comm, sizeof(comm));
	}
	event.freq = value;
	event.kt = ktime_get();
	event.oem = ctx;
	trace_request(ctx, addition ? "A" : "U", value);
	spin_lock_irqsave(&monitor_lock, flags);
	if (!READ_ONCE(stopping)) {
		ctx->history[ctx->total++ % HISTORY_COUNT] = event;
		old = stream[stream_total % STREAM_COUNT].oem;
		kref_get(&ctx->refs);
		stream[stream_total % STREAM_COUNT] = event;
		WRITE_ONCE(stream_total, stream_total + 1);
		context_put(old);
	}
	spin_unlock_irqrestore(&monitor_lock, flags);
	wake_up_interruptible(&stream_wait);
	context_put(ctx);
}
static noinline void qos_add_request(void *unused, struct freq_constraints *qos,
		struct freq_qos_request *req, enum freq_qos_req_type type,
		int value, int ret)
{
	record_request(req, value,
		(unsigned long)__builtin_extract_return_addr(__builtin_return_address(1)),
		(unsigned long)__builtin_extract_return_addr(__builtin_return_address(2)), true);
}
static noinline void qos_update_request(void *unused, struct freq_qos_request *req,
				       int value)
{
	record_request(req, value,
		(unsigned long)__builtin_extract_return_addr(__builtin_return_address(1)),
		(unsigned long)__builtin_extract_return_addr(__builtin_return_address(2)), false);
}
static void qos_remove_request(void *unused, struct freq_qos_request *req)
{
	struct qos_context *ctx;
	unsigned long flags;

	spin_lock_irqsave(&monitor_lock, flags);
	ctx = find_context(req);
	if (ctx)
		hash_del(&ctx->node);
	spin_unlock_irqrestore(&monitor_lock, flags);
	if (ctx) {
		trace_request(ctx, "R", 0);
		context_put(ctx);
	}
}

struct snapshot {
	struct seq_file *seq;
	struct cpufreq_policy *policy;
	unsigned int mode;
};
static void show_request(const struct freq_qos_request *req, s32 value, void *data)
{
	struct snapshot *snapshot = data;
	struct qos_context *ctx;
	struct timespec64 ts;
	struct freq_qos_record *event;
	unsigned long flags;
	u64 start, index;

	if ((snapshot->mode & 2) &&
	    (value <= snapshot->policy->cpuinfo.min_freq ||
	     value >= snapshot->policy->cpuinfo.max_freq))
		return;
	spin_lock_irqsave(&monitor_lock, flags);
	ctx = find_context(req);
	if (!ctx) {
		seq_printf(snapshot->seq, "\tmodules=[not-hooked] freq=%d\n", value);
		goto unlock;
	}
	if (snapshot->policy->cpu != ctx->cpu)
		seq_printf(snapshot->seq, "\t[Warning] policy cpu:%d != req cpu:%d\n",
			snapshot->policy->cpu, ctx->cpu);
	seq_printf(snapshot->seq, "\tmodules=%s freq=%d\n", ctx->source, value);
	if (snapshot->mode & 1)
		goto unlock;
	if (ctx->total)
		seq_printf(snapshot->seq, "\t  process_info: last %d / %lu\n",
			(unsigned int)min_t(u64, ctx->total, HISTORY_COUNT),
			(unsigned long)ctx->total);
	start = ctx->total > HISTORY_COUNT ? ctx->total - HISTORY_COUNT : 0;
	for (index = start; index < ctx->total; index++) {
		event = &ctx->history[index % HISTORY_COUNT];
		ts = ktime_to_timespec64(event->kt);
		seq_printf(snapshot->seq,
			"\t\t[%6lld.%06ld] %d/%s tgid:%d freq:%d\n",
			ts.tv_sec, ts.tv_nsec / 1000, event->pid, event->comm,
			event->tgid, event->freq);
	}
unlock:
	spin_unlock_irqrestore(&monitor_lock, flags);
}
static int freq_qos_show(struct seq_file *seq, void *unused)
{
	struct cpufreq_policy *policy;
	struct snapshot snapshot = { .seq = seq, .mode = (unsigned long)seq->private };
	int cpu, member;

	seq_puts(seq, "mz freq qos-------------------------------------------------------------\n");
	seq_puts(seq, proc_descriptions[snapshot.mode]);
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (policy->cpu != cpu) {
			cpufreq_cpu_put(policy);
			continue;
		}
		snapshot.policy = policy;
		seq_printf(seq, "============= policy-%d==================\n", policy->cpu);
		seq_puts(seq, "cpu:");
		for_each_cpu(member, policy->cpus)
			seq_printf(seq, " %d", member);
		seq_putc(seq, '\n');
		seq_printf(seq, "policy-%d min:(%d)\n", cpu, policy->cpuinfo.min_freq);
		meizu_freq_qos_walk(&policy->constraints, FREQ_QOS_MIN,
				    show_request, &snapshot);
		seq_printf(seq, "policy-%d max:(%d)\n", cpu, policy->cpuinfo.max_freq);
		meizu_freq_qos_walk(&policy->constraints, FREQ_QOS_MAX,
				    show_request, &snapshot);
		cpufreq_cpu_put(policy);
	}
	return 0;
}
static int freq_qos_open(struct inode *inode, struct file *file)
{
	return single_open(file, freq_qos_show, PDE_DATA(inode));
}
static const struct proc_ops snapshot_ops = {
	.proc_open = freq_qos_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int qos_log_open(struct inode *inode, struct file *file)
{
	struct log_reader *reader;
	unsigned long flags;

	if (atomic_cmpxchg(&log_opened, 0, 1))
		return -EBUSY;
	reader = kzalloc(sizeof(*reader), GFP_KERNEL);
	if (!reader) {
		atomic_set(&log_opened, 0);
		return -ENOMEM;
	}
	spin_lock_irqsave(&monitor_lock, flags);
	reader->cursor = stream_total > STREAM_COUNT ? stream_total - STREAM_COUNT : 0;
	spin_unlock_irqrestore(&monitor_lock, flags);
	mutex_init(&reader->lock);
	file->private_data = reader;
	return 0;
}
static ssize_t qos_log_read_locked(struct file *file, char __user *buf, size_t count,
			    loff_t *pos)
{
	struct log_reader *reader = file->private_data;
	struct freq_qos_record event;
	struct qos_context *ctx;
	struct timespec64 ts;
	unsigned long flags;
	size_t copied;
	int ret;

	if (!count)
		return 0;
	if (reader->offset < reader->length)
		goto copy;
	for (;;) {
		spin_lock_irqsave(&monitor_lock, flags);
		if (READ_ONCE(stopping)) {
			spin_unlock_irqrestore(&monitor_lock, flags);
			return 0;
		}
		if (stream_total - reader->cursor > STREAM_COUNT)
			reader->cursor = stream_total - STREAM_COUNT;
		if (reader->cursor < stream_total) {
			event = stream[reader->cursor++ % STREAM_COUNT];
			ctx = event.oem;
			kref_get(&ctx->refs);
			spin_unlock_irqrestore(&monitor_lock, flags);
			break;
		}
		spin_unlock_irqrestore(&monitor_lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(stream_wait, READ_ONCE(stopping) ||
			READ_ONCE(stream_total) != reader->cursor);
		if (ret)
			return ret;
	}
	ts = ktime_to_timespec64(event.kt);
	reader->length = scnprintf(reader->text, sizeof(reader->text),
		"[%6lld.%06ld] policy-%u (%s) %d/%s tgid:%d freq:%d [%s]\n",
		ts.tv_sec, ts.tv_nsec / 1000, ctx->cpu, ctx->source,
		event.pid, event.comm, event.tgid, event.freq,
		event.freq > ctx->min_freq && event.freq < ctx->max_freq ? "valid" : "inv");
	reader->offset = 0;
	context_put(ctx);
copy:
	copied = min(count, reader->length - reader->offset);
	if (copy_to_user(buf, reader->text + reader->offset, copied))
		return -EFAULT;
	reader->offset += copied;
	return copied;
}
static ssize_t qos_log_read(struct file *file, char __user *buf, size_t count,
			    loff_t *pos)
{
	struct log_reader *reader = file->private_data;
	ssize_t ret;

	if (mutex_lock_interruptible(&reader->lock))
		return -ERESTARTSYS;
	ret = qos_log_read_locked(file, buf, count, pos);
	mutex_unlock(&reader->lock);
	return ret;
}
static int qos_log_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	atomic_set(&log_opened, 0);
	return 0;
}
static const struct proc_ops log_ops = {
	.proc_open = qos_log_open,
	.proc_read = qos_log_read,
	.proc_release = qos_log_release,
	.proc_lseek = no_llseek,
};
static ssize_t systrace_read(struct file *file, char __user *buf, size_t count,
			     loff_t *pos)
{
	char text[30];
	int len = scnprintf(text, sizeof(text), "%s\n",
		READ_ONCE(systrace_en) ? "enabled" : "disabed");

	return simple_read_from_buffer(buf, count, pos, text, len);
}
static ssize_t systrace_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	char text[20] = {};
	size_t copied = min(count, sizeof(text) - 1);

	if (copy_from_user(text, buf, copied))
		return -EFAULT;
	WRITE_ONCE(systrace_en, text[0] != '0');
	return copied;
}
static const struct proc_ops systrace_ops = {
	.proc_read = systrace_read,
	.proc_write = systrace_write,
};
static void free_state(void)
{
	struct qos_context *ctx;
	struct hlist_node *next;
	unsigned long flags;
	int bucket, i;

	spin_lock_irqsave(&monitor_lock, flags);
	hash_for_each_safe(contexts, bucket, next, ctx, node) {
		hash_del(&ctx->node);
		context_put(ctx);
	}
	for (i = 0; i < STREAM_COUNT; i++) {
		context_put(stream[i].oem);
		stream[i].oem = NULL;
	}
	spin_unlock_irqrestore(&monitor_lock, flags);
}
static int __init mz_freq_qos_init(void)
{
	struct device_node *bootinfo;
	u32 sec = 0, dp_exist = 0;
	int ret, i;
	static const char * const names[] = { "all", "all_no_proc", "valid", "valid_no_proc" };

	bootinfo = of_find_node_by_path("/bootinfo");
	if (bootinfo) {
		if (!of_property_read_u32(bootinfo, "sec", &sec) &&
		    !of_property_read_u32(bootinfo, "dp_exist", &dp_exist))
			systrace_en = !sec || dp_exist;
		of_node_put(bootinfo);
	}
	proc_dir = proc_mkdir("mz_freq_qos", NULL);
	if (!proc_dir)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(names); i++)
		if (!proc_create_data(names[i], 0440, proc_dir, &snapshot_ops,
				      (void *)(unsigned long)i))
			goto fail_proc;
	if (!proc_create("log", 0440, proc_dir, &log_ops) ||
	    !proc_create("systrace_en", 0660, proc_dir, &systrace_ops))
		goto fail_proc;
	ret = register_trace_android_vh_freq_qos_add_request(qos_add_request, NULL);
	if (ret)
		goto remove_proc;
	ret = register_trace_android_vh_freq_qos_update_request(qos_update_request, NULL);
	if (ret)
		goto unregister_add;
	ret = register_trace_android_vh_freq_qos_remove_request(qos_remove_request, NULL);
	if (!ret)
		return 0;
	unregister_trace_android_vh_freq_qos_update_request(qos_update_request, NULL);
unregister_add:
	unregister_trace_android_vh_freq_qos_add_request(qos_add_request, NULL);
	tracepoint_synchronize_unregister();
remove_proc:
	WRITE_ONCE(stopping, true);
	wake_up_interruptible(&stream_wait);
	proc_remove(proc_dir);
	free_state();
	return ret;
fail_proc:
	ret = -ENOMEM;
	goto remove_proc;
}
static void __exit mz_freq_qos_exit(void)
{
	WRITE_ONCE(stopping, true);
	wake_up_interruptible(&stream_wait);
	unregister_trace_android_vh_freq_qos_add_request(qos_add_request, NULL);
	unregister_trace_android_vh_freq_qos_update_request(qos_update_request, NULL);
	unregister_trace_android_vh_freq_qos_remove_request(qos_remove_request, NULL);
	tracepoint_synchronize_unregister();
	proc_remove(proc_dir);
	free_state();
}
module_init(mz_freq_qos_init);
module_exit(mz_freq_qos_exit);
MODULE_DESCRIPTION("Meizu frequency QoS diagnostics");
MODULE_LICENSE("GPL");
