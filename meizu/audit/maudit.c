// SPDX-License-Identifier: GPL-2.0-only
/* M2391 original maudit.ko: native ioctl and log layouts are original BTF;
 * mode 1/2/3 accounting is .text+0x1a44..0x1d38, FIFO read .text+0xb44.
 * Per-open ownership, three ioctl commands and all four proc controls are
 * implemented. flags is intentionally only a stored/displayed value: original
 * pre_handler/tick never consult it. No probe is installed at module init.
 * Probe teardown waits for callbacks, drains proc users and retains queued
 * logger references. poll's original missing readiness bits are repaired.
 * Directory names are confined to one component; copy-fault requeue wakes
 * readers; allocation/copy-out failures return proper negative errors.
 */
#include <linux/cred.h>
#include <linux/jiffies.h>
#include <linux/kprobes.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "maudit_protocol.h"

#if !IS_ENABLED(CONFIG_KPROBES)
#error "maudit requires the actual CONFIG_KPROBES implementation"
#endif

static_assert(sizeof(struct maudit_set_name) == 264);
static_assert(sizeof(struct maudit_kprobe_desc) == 288);
static_assert(offsetof(struct maudit_kprobe_desc, name) == 28);
static_assert(sizeof(struct maudit_log_entry) == 24);
static_assert(sizeof(struct maudit_logger) == 40);
static_assert(offsetof(struct maudit_logger, entrys) == 16);

struct maudit_user;
struct maudit_probe {
	struct maudit_kprobe_desc desc;
	struct kprobe probe;
	struct maudit_user *owner;
	struct proc_dir_entry *directory;
	spinlock_t lock;
	struct list_head available;
	struct kref references;
	u32 count;
	unsigned long last_tick;
	bool registered;
	bool flags;
};
struct maudit_record {
	struct list_head node;
	struct maudit_logger raw;
	struct maudit_probe *probe;
};
struct maudit_user {
	char name[256];
	struct proc_dir_entry *directory;
	struct mutex probes_lock;
	struct maudit_probe *probes[MAUDIT_MAX_PROBES];
	spinlock_t records_lock;
	struct list_head records;
	wait_queue_head_t waitqueue;
};
static struct proc_dir_entry *maudit_root;

static void probe_release(struct kref *reference)
{
	struct maudit_probe *probe = container_of(reference, struct maudit_probe, references);

	kfree(probe);
}

NOKPROBE_SYMBOL(probe_release);

static void record_handled(struct maudit_record *record)
{
	struct maudit_probe *probe = record->probe;
	unsigned long flags;
	bool recycle;

	spin_lock_irqsave(&probe->lock, flags);
	recycle = probe->registered;
	if (recycle)
		list_add_tail(&record->node, &probe->available);
	spin_unlock_irqrestore(&probe->lock, flags);
	if (!recycle)
		kfree(record);
	kref_put(&probe->references, probe_release);
}

NOKPROBE_SYMBOL(record_handled);

static int maudit_kprobe_pre_handler(struct kprobe *kp, struct pt_regs *regs)
{
	struct maudit_probe *probe = container_of(kp, struct maudit_probe, probe);
	struct maudit_user *owner = probe->owner;
	struct maudit_record *record;
	unsigned long flags, now = jiffies, interval;
	u32 uid = __kuid_val(current_uid());
	bool tick = false;

	if (uid < probe->desc.uid_range[0] || uid > probe->desc.uid_range[1])
		return 0;
	spin_lock_irqsave(&probe->lock, flags);
	if (!probe->registered)
		goto unlock;
	/* Original uses a wrapped u32 multiplication before division. */
	interval = (u32)(probe->desc.overtime_ms * HZ) / 1000;
	switch (probe->desc.mode) {
	case 1:
		if (time_after(now, probe->last_tick + interval)) {
			probe->last_tick = now;
			tick = true;
		}
		break;
	case 2:
		probe->count++;
		if ((unsigned long)(long)(s32)probe->count >= probe->desc.threshold) {
			probe->count -= probe->desc.threshold;
			tick = true;
		}
		break;
	case 3:
		if (time_after(now, probe->last_tick + interval)) {
			probe->last_tick = now;
			probe->count = 0;
		} else {
			probe->count++;
			if ((unsigned long)(long)(s32)probe->count >= probe->desc.threshold) {
				probe->last_tick = now;
				probe->count -= probe->desc.threshold;
				tick = true;
			}
		}
		break;
	}
	if (!tick || list_empty(&probe->available))
		goto unlock;
	record = list_first_entry(&probe->available, struct maudit_record, node);
	list_del_init(&record->node);
	kref_get(&probe->references);
	memset(&record->raw, 0, sizeof(record->raw));
	record->raw.probeid = probe->desc.probeid;
	record->raw.logcount = 1;
	record->raw.entrys[0].pid = current->tgid;
	record->raw.entrys[0].tid = current->pid;
	record->raw.entrys[0].uid = uid;
	record->raw.entrys[0].jiffies = jiffies;
	record->probe = probe;
	spin_unlock_irqrestore(&probe->lock, flags);
	spin_lock_irqsave(&owner->records_lock, flags);
	list_add_tail(&record->node, &owner->records);
	spin_unlock_irqrestore(&owner->records_lock, flags);
	wake_up(&owner->waitqueue);
	pr_info("%s: %s tick\n", "maudit_kprobe_tick", probe->desc.name);
	return 0;
unlock:
	spin_unlock_irqrestore(&probe->lock, flags);
	return 0;
}

NOKPROBE_SYMBOL(maudit_kprobe_pre_handler);

static int probe_id_show(struct seq_file *seq, void *unused)
{
	struct maudit_probe *probe = seq->private;

	seq_printf(seq, "%llu\n", (unsigned long long)probe->desc.probeid);
	return 0;
}
static int overtime_show(struct seq_file *seq, void *unused)
{
	struct maudit_probe *probe = seq->private;

	seq_printf(seq, "%llu\n", (unsigned long long)READ_ONCE(probe->desc.overtime_ms));
	return 0;
}
static int threshold_show(struct seq_file *seq, void *unused)
{
	struct maudit_probe *probe = seq->private;

	seq_printf(seq, "%llu\n", (unsigned long long)READ_ONCE(probe->desc.threshold));
	return 0;
}
static int flags_show(struct seq_file *seq, void *unused)
{
	struct maudit_probe *probe = seq->private;

	seq_printf(seq, "%llu\n", (unsigned long long)READ_ONCE(probe->flags));
	return 0;
}
#define MAUDIT_OPEN(name, show) \
static int name(struct inode *inode, struct file *file) \
{ return single_open(file, show, PDE_DATA(inode)); }
MAUDIT_OPEN(probe_id_open, probe_id_show)
MAUDIT_OPEN(overtime_open, overtime_show)
MAUDIT_OPEN(threshold_open, threshold_show)
MAUDIT_OPEN(flags_open, flags_show)

enum control_kind { CONTROL_FLAGS, CONTROL_OVERTIME, CONTROL_THRESHOLD };
static ssize_t control_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *offset, enum control_kind kind)
{
	struct maudit_probe *probe = PDE_DATA(file_inode(file));
	char text[64];
	unsigned long irq_flags;
	unsigned long long enabled;
	unsigned int value;

	if (count > 63 || *offset)
		return -EINVAL;
	if (copy_from_user(text, buffer, count))
		return -EFAULT;
	text[count] = '\0';
	if (kind == CONTROL_FLAGS) {
		if (sscanf(text, "%llu", &enabled) != 1)
			return -EINVAL;
	} else if (sscanf(text, "%u", &value) != 1)
		return -EINVAL;
	spin_lock_irqsave(&probe->lock, irq_flags);
	if (kind == CONTROL_FLAGS)
		probe->flags = !!enabled;
	else if (kind == CONTROL_OVERTIME)
		probe->desc.overtime_ms = value;
	else
		probe->desc.threshold = value;
	spin_unlock_irqrestore(&probe->lock, irq_flags);
	return count;
}
#define MAUDIT_WRITE(name, kind) \
static ssize_t name(struct file *file, const char __user *buffer, size_t count, loff_t *offset) \
{ return control_write(file, buffer, count, offset, kind); }
MAUDIT_WRITE(flags_write, CONTROL_FLAGS)
MAUDIT_WRITE(overtime_write, CONTROL_OVERTIME)
MAUDIT_WRITE(threshold_write, CONTROL_THRESHOLD)
#define MAUDIT_PROC(name, open, write) \
static const struct proc_ops name = { .proc_open = open, .proc_read = seq_read, \
	.proc_write = write, .proc_lseek = seq_lseek, .proc_release = single_release }
MAUDIT_PROC(id_ops, probe_id_open, NULL);
MAUDIT_PROC(flags_ops, flags_open, flags_write);
MAUDIT_PROC(overtime_ops, overtime_open, overtime_write);
MAUDIT_PROC(threshold_ops, threshold_open, threshold_write);

static int create_probe_proc(struct maudit_user *user, struct maudit_probe *probe)
{
	if (!user->directory)
		return 0; /* Original permits probes before SET_NAME, with no proc tree. */
	probe->directory = proc_mkdir(probe->desc.name, user->directory);
	if (!probe->directory)
		return -ENOMEM;
	if (!proc_create_data("proc_id", 0400, probe->directory, &id_ops, probe) ||
	    !proc_create_data("overtime_ms", 0600, probe->directory, &overtime_ops, probe) ||
	    !proc_create_data("threshold", 0600, probe->directory, &threshold_ops, probe) ||
	    !proc_create_data("flags", 0600, probe->directory, &flags_ops, probe)) {
		proc_remove(probe->directory);
		probe->directory = NULL;
		return -ENOMEM;
	}
	return 0;
}

static void destroy_probe(struct maudit_probe *probe)
{
	LIST_HEAD(available);
	struct maudit_record *record, *next;
	unsigned long flags;

	/* No proc operation can retain/use the object after this drain. */
	proc_remove(probe->directory);
	probe->directory = NULL;
	spin_lock_irqsave(&probe->lock, flags);
	probe->registered = false;
	spin_unlock_irqrestore(&probe->lock, flags);
	unregister_kprobe(&probe->probe);
	spin_lock_irqsave(&probe->lock, flags);
	list_splice_init(&probe->available, &available);
	spin_unlock_irqrestore(&probe->lock, flags);
	list_for_each_entry_safe(record, next, &available, node) {
		list_del(&record->node);
		kfree(record);
	}
	/* Queued/read-in-progress records own the remaining references. */
	kref_put(&probe->references, probe_release);
}

static bool valid_component(const char *name)
{
	return *name && !strchr(name, '/') && strcmp(name, ".") &&
		strcmp(name, "..");
}

static int register_probe(struct maudit_user *user, void __user *argument)
{
	struct maudit_probe *probe;
	struct maudit_record *record, *next;
	unsigned int slot, i;
	unsigned long flags;
	int ret;

	probe = kzalloc(sizeof(*probe), GFP_KERNEL);
	if (!probe)
		return -ENOMEM;
	if (copy_from_user(&probe->desc, argument, sizeof(probe->desc))) {
		ret = -EACCES; /* Original ioctl copy fault errno. */
		goto free_probe;
	}
	probe->desc.name[255] = '\0';
	if (!valid_component(probe->desc.name) ||
	    probe->desc.logcount >= MAUDIT_MAX_PROBES) {
		ret = -EINVAL;
		goto free_probe;
	}
	for (slot = 0; slot < MAUDIT_MAX_PROBES; slot++)
		if (!user->probes[slot])
			break;
	if (slot == MAUDIT_MAX_PROBES) {
		ret = -ENOSPC;
		goto free_probe;
	}
	probe->desc.probeid = slot;
	probe->owner = user;
	spin_lock_init(&probe->lock);
	INIT_LIST_HEAD(&probe->available);
	kref_init(&probe->references);
	for (i = 0; i < probe->desc.logcount; i++) {
		record = kzalloc(sizeof(*record), GFP_KERNEL);
		if (!record) {
			ret = -ENOMEM;
			goto free_records;
		}
		list_add_tail(&record->node, &probe->available);
	}
	probe->probe.symbol_name = probe->desc.name;
	probe->probe.pre_handler = maudit_kprobe_pre_handler;
	probe->registered = true;
	ret = register_kprobe(&probe->probe);
	if (ret)
		goto free_records;
	user->probes[slot] = probe;
	ret = create_probe_proc(user, probe);
	if (!ret && copy_to_user(argument, &probe->desc, sizeof(probe->desc)))
		ret = -EACCES;
	if (ret) {
		user->probes[slot] = NULL;
		destroy_probe(probe);
		return ret;
	}
	/* Original starts/restarts its interval only after successful copy-out. */
	spin_lock_irqsave(&probe->lock, flags);
	probe->last_tick = jiffies;
	probe->count = 0;
	spin_unlock_irqrestore(&probe->lock, flags);
	return 0;
free_records:
	list_for_each_entry_safe(record, next, &probe->available, node) {
		list_del(&record->node);
		kfree(record);
	}
free_probe:
	kfree(probe);
	return ret;
}

static long maudit_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
	struct maudit_user *user = file->private_data;
	struct maudit_set_name name;
	struct maudit_probe *probe;
	long ret = 0;

	mutex_lock(&user->probes_lock);
	switch (command) {
	case MAUDIT_SET_NAME:
		if (user->directory) { ret = -EINVAL; break; }
		if (copy_from_user(&name, (void __user *)argument, sizeof(name))) {
			ret = -EACCES;
			break;
		}
		if (name.version != MAUDIT_VERSION) { ret = -EINVAL; break; }
		name.name[255] = '\0';
		if (!valid_component(name.name)) { ret = -EINVAL; break; }
		user->directory = proc_mkdir(name.name, maudit_root);
		if (!user->directory) { ret = -ENOMEM; break; }
		memcpy(user->name, name.name, sizeof(user->name));
		break;
	case MAUDIT_REGISTER_PROBE:
		ret = register_probe(user, (void __user *)argument);
		break;
	case MAUDIT_UNREGISTER_PROBE:
		/* Despite encoded _IOW size 288, original third argument is the ID. */
		if (argument >= MAUDIT_MAX_PROBES || !user->probes[argument]) {
			ret = -EINVAL;
			break;
		}
		probe = user->probes[argument];
		user->probes[argument] = NULL;
		destroy_probe(probe);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&user->probes_lock);
	return ret;
}

static bool records_available(struct maudit_user *user)
{
	unsigned long flags;
	bool available;

	spin_lock_irqsave(&user->records_lock, flags);
	available = !list_empty(&user->records);
	spin_unlock_irqrestore(&user->records_lock, flags);
	return available;
}
static ssize_t maudit_read(struct file *file, char __user *buffer, size_t count, loff_t *offset)
{
	struct maudit_user *user = file->private_data;
	struct maudit_record *record;
	unsigned long flags;
	int ret;

	if (count < sizeof(record->raw) || *offset)
		return -EINVAL;
	for (;;) {
		ret = wait_event_interruptible(user->waitqueue, records_available(user));
		if (ret)
			return -EINTR;
		spin_lock_irqsave(&user->records_lock, flags);
		if (list_empty(&user->records)) {
			spin_unlock_irqrestore(&user->records_lock, flags);
			continue;
		}
		record = list_first_entry(&user->records, struct maudit_record, node);
		list_del_init(&record->node);
		spin_unlock_irqrestore(&user->records_lock, flags);
		break;
	}
	if (copy_to_user(buffer, &record->raw, sizeof(record->raw))) {
		spin_lock_irqsave(&user->records_lock, flags);
		list_add(&record->node, &user->records);
		spin_unlock_irqrestore(&user->records_lock, flags);
		wake_up(&user->waitqueue);
		return -EINVAL;
	}
	record_handled(record);
	return sizeof(struct maudit_logger);
}
static __poll_t maudit_poll(struct file *file, poll_table *table)
{
	struct maudit_user *user = file->private_data;

	poll_wait(file, &user->waitqueue, table);
	return records_available(user) ? EPOLLIN | EPOLLRDNORM : 0;
}

static int maudit_open(struct inode *inode, struct file *file)
{
	struct maudit_user *user = kzalloc(sizeof(*user), GFP_KERNEL);

	if (!user)
		return -ENOMEM;
	strscpy(user->name, "default", sizeof(user->name));
	mutex_init(&user->probes_lock);
	spin_lock_init(&user->records_lock);
	INIT_LIST_HEAD(&user->records);
	init_waitqueue_head(&user->waitqueue);
	file->private_data = user;
	return 0;
}
static int maudit_release(struct inode *inode, struct file *file)
{
	struct maudit_user *user = file->private_data;
	struct maudit_record *record, *next;
	unsigned int slot;

	for (slot = 0; slot < MAUDIT_MAX_PROBES; slot++)
		if (user->probes[slot])
			destroy_probe(user->probes[slot]);
	/* All registered handlers are gone; no producer can touch this queue. */
	list_for_each_entry_safe(record, next, &user->records, node) {
		list_del(&record->node);
		record_handled(record);
	}
	proc_remove(user->directory);
	kfree(user);
	return 0;
}
static const struct file_operations maudit_fops = {
	.owner = THIS_MODULE,
	.open = maudit_open,
	.release = maudit_release,
	.read = maudit_read,
	.poll = maudit_poll,
	.unlocked_ioctl = maudit_ioctl,
};
static struct miscdevice maudit_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "maudit",
	.fops = &maudit_fops,
};
static int __init maudit_init(void)
{
	int ret;

	maudit_root = proc_mkdir("maudit", NULL);
	if (!maudit_root)
		return -ENOMEM;
	ret = misc_register(&maudit_miscdevice);
	if (ret)
		proc_remove(maudit_root);
	return ret;
}
static void __exit maudit_exit(void)
{
	misc_deregister(&maudit_miscdevice);
	proc_remove(maudit_root);
}
module_init(maudit_init);
module_exit(maudit_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Even<chenyuwen1@meizu.com>");
