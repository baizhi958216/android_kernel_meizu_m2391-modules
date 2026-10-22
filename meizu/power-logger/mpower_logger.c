// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 mpower_logger recovery from stock .text: print0x170, open0x610,
 * read0x6b4, write0x920; base+split BTF supplies all private field layouts.
 * 128KiB ring, local realtime calendar + monotonic-seconds prefixes, blocking
 * per-reader streams with4KiB staging, overrun skips first partial line.
 * proc write accepts the stock binary64-bit backlog-threshold command; only
 * its low32 bits were used originally. No text parser or invented commands.
 * Fixes: zero-size reads return immediately; short binary writes cannot
 * overread user memory; readers are serialized and exit wakes blocked reads
 * before removing procfs. No usercopy or allocation occurs under ring lock.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include "../power.h"

#define MPOWER_RING_SIZE (128 * 1024)
#define MPOWER_RING_MASK (MPOWER_RING_SIZE - 1)
#define MPOWER_MAGIC 0x4347424d
#define MPOWER_SCRATCH_SIZE 2048
#define MPOWER_READ_SIZE 4096

struct dram_console_buffer {
	uint32_t sig;
	uint32_t datalen;
	uint32_t datalen_mask;
	uint64_t start;
	char data[MPOWER_RING_SIZE];
};
static struct dram_console_buffer mpower_console_buffer;
static DEFINE_SPINLOCK(mpower_spinlock);
static DECLARE_WAIT_QUEUE_HEAD(usr_wq);
static char scratch[MPOWER_SCRATCH_SIZE];
static bool stopping;
static struct proc_dir_entry *proc_entry;

struct dram_console_reader {
	uint64_t offset;
	int block_len;
	int buflen;
	char *buffer;
	struct mutex lock;
};

static void initialize_ring(void)
{
	if (mpower_console_buffer.sig == MPOWER_MAGIC)
		return;
	memset(&mpower_console_buffer, 0, sizeof(mpower_console_buffer));
	mpower_console_buffer.sig = MPOWER_MAGIC;
	mpower_console_buffer.datalen = MPOWER_RING_SIZE;
	mpower_console_buffer.datalen_mask = MPOWER_RING_MASK;
}

/* Caller holds mpower_spinlock, preserving the stock wrap/truncation rule. */
static int ring_vstore(char *format, va_list args)
{
	unsigned int position = mpower_console_buffer.start & MPOWER_RING_MASK;
	unsigned int room = MPOWER_RING_SIZE - position;
	va_list probe;
	int required, total, i;

	/* Probe the length before touching the ring. This preserves the stock
	 * visible truncation rule without writing bytes past the committed end.
	 */
	va_copy(probe, args);
	required = vsnprintf(NULL, 0, format, probe);
	va_end(probe);
	if (required >= room - 1) {
		total = vscnprintf(scratch, sizeof(scratch), format, args);
		for (i = 0; i < total; i++)
			mpower_console_buffer.data[(position + i) & MPOWER_RING_MASK] = scratch[i];
	} else {
		total = vscnprintf(mpower_console_buffer.data + position, room, format, args);
	}
	mpower_console_buffer.start += total;
	return total;
}
static int ring_store(char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = ring_vstore(format, args);
	va_end(args);
	return ret;
}

int mpower_print(char *format, ...)
{
	struct timespec64 realtime, monotonic;
	struct rtc_time tm;
	unsigned long flags;
	va_list args;
	int count;

	ktime_get_real_ts64(&realtime);
	rtc_time64_to_tm(realtime.tv_sec - (s64)sys_tz.tz_minuteswest * 60, &tm);
	ktime_get_ts64(&monotonic);
	va_start(args, format);
	spin_lock_irqsave(&mpower_spinlock, flags);
	initialize_ring();
	count = ring_store("[%d-%02d-%02d %02d:%02d:%02d]", tm.tm_year + 1900,
			tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	count += ring_store("[%5lld]", monotonic.tv_sec);
	count += ring_vstore(format, args);
	spin_unlock_irqrestore(&mpower_spinlock, flags);
	va_end(args);
	wake_up_interruptible(&usr_wq);
	return count;
}
EXPORT_SYMBOL(mpower_print);

static int dram_console_open(struct inode *inode, struct file *file)
{
	struct dram_console_reader *reader;

	reader = kzalloc(sizeof(*reader), GFP_KERNEL);
	if (!reader)
		return -ENOMEM;
	reader->buffer = kmalloc(MPOWER_READ_SIZE, GFP_KERNEL);
	if (!reader->buffer) {
		kfree(reader);
		return -ENOMEM;
	}
	reader->buflen = MPOWER_READ_SIZE;
	mutex_init(&reader->lock);
	file->private_data = reader;
	return 0;
}

static bool reader_ready(struct dram_console_reader *reader)
{
	return READ_ONCE(stopping) ||
		READ_ONCE(reader->offset) + READ_ONCE(reader->block_len) <
		READ_ONCE(mpower_console_buffer.start);
}

static ssize_t dram_console_read(struct file *file, char __user *buf,
		size_t count, loff_t *position)
{
	struct dram_console_reader *reader = file->private_data;
	unsigned long flags;
	u64 offset, available;
	size_t copied, i;
	int ret;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&reader->lock))
		return -EIO;
again:
	ret = wait_event_interruptible(usr_wq, reader_ready(reader));
	if (ret) {
		ret = -EIO; /* Original observable signal return. */
		goto out;
	}
	if (READ_ONCE(stopping)) {
		ret = 0;
		goto out;
	}
	count = min_t(size_t, count, reader->buflen);
	spin_lock_irqsave(&mpower_spinlock, flags);
	offset = reader->offset;
	available = mpower_console_buffer.start - offset;
	if (available >= MPOWER_RING_SIZE) {
		offset = mpower_console_buffer.start - MPOWER_RING_SIZE;
		/* Do not expose a line whose first bytes were overwritten. */
		for (i = 0; i < MPOWER_RING_SIZE; i++) {
			char c = mpower_console_buffer.data[offset++ & MPOWER_RING_MASK];

			if (c == '\n')
				break;
		}
	}
	available = mpower_console_buffer.start - offset;
	copied = min_t(u64, count, available);
	for (i = 0; i < copied; i++)
		reader->buffer[i] = mpower_console_buffer.data[(offset + i) & MPOWER_RING_MASK];
	spin_unlock_irqrestore(&mpower_spinlock, flags);
	if (!copied) {
		/* An overwritten unterminated line is not end-of-file. */
		reader->offset = offset;
		goto again;
	}
	if (copy_to_user(buf, reader->buffer, copied)) {
		ret = -EFAULT;
		goto out;
	}
	reader->offset = offset + copied;
	ret = copied;
out:
	mutex_unlock(&reader->lock);
	return ret;
}

static ssize_t dram_console_write(struct file *file, const char __user *buf,
		size_t count, loff_t *position)
{
	struct dram_console_reader *reader = file->private_data;
	u64 command;

	if (count < sizeof(command))
		return -EINVAL;
	if (copy_from_user(&command, buf, sizeof(command)))
		return -EFAULT;
	if ((u32)command > INT_MAX)
		return -EINVAL;
	/* Atomic scalar update lets another thread adjust a blocked reader. */
	WRITE_ONCE(reader->block_len, (int)(u32)command);
	wake_up_interruptible(&usr_wq);
	return count;
}

static int dram_console_release(struct inode *inode, struct file *file)
{
	struct dram_console_reader *reader = file->private_data;

	kfree(reader->buffer);
	kfree(reader);
	return 0;
}
static const struct proc_ops ram_console_fops = {
	.proc_open = dram_console_open,
	.proc_read = dram_console_read,
	.proc_write = dram_console_write,
	.proc_release = dram_console_release,
};

static int __init mpower_logger_init(void)
{
	unsigned long flags;

	spin_lock_irqsave(&mpower_spinlock, flags);
	initialize_ring();
	spin_unlock_irqrestore(&mpower_spinlock, flags);
	proc_entry = proc_create("mpower_kmsg", 0666, NULL, &ram_console_fops);
	return proc_entry ? 0 : -ENOMEM;
}
static void __exit mpower_logger_exit(void)
{
	WRITE_ONCE(stopping, true);
	wake_up_interruptible_all(&usr_wq);
	proc_remove(proc_entry);
}
module_init(mpower_logger_init);
module_exit(mpower_logger_exit);
MODULE_LICENSE("GPL v2");
