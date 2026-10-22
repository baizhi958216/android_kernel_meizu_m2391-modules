// SPDX-License-Identifier: GPL-2.0-only
/* M2391 reset counters, kernel-log slots and original proc command protocol.
 * The on-disk format is kept verbatim; malformed indices and short/error reads
 * must not become persistent writes. Recovery evidence: original annotated
 * .text+0x5e8..0x2238 and BTF format declarations in mz_rstinfo_format.h.
 */
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include "../storage/meizu_storage.h"
#include "mz_rstinfo_internal.h"
#include "mz_rstinfo_strings.h"
#define RSTINFO_MAGIC 0xaeeabced
#define RSTINFO_VERSION 12
#define RSTCNT_OFFSET 0x3000
#define RSTCNT_SIZE 0x70000
#define RSTLOG_OFFSET 0x73000
#define RSTLOG_SIZE 0x800000
#define RSTSSN_OFFSET 0x873000
#define RSTSSN_SIZE 4096
#define RSTSSR_OFFSET 0x874000
#define RSTSSR_SIZE 0x100000
#define RSTLOG_ENTITY_SIZE (RSTLOG_SIZE / 16)
#define CURRENT_LOG_OFFSET 0x1200000
struct persistent_ram_buffer {
 u32 sig;
 int start, size;
 char data[];
};
static struct rstinfo_header rstinfo_head;
static DEFINE_MUTEX(rstinfo_lock);
static DEFINE_MUTEX(registry_lock);
static struct kobject *rstinfo_kobj;
static struct proc_dir_entry *proc_nodes[7];
static struct delayed_work rstinfo_work;
static struct timer_list encrypt_timer;
static bool stopping, rstinfo_ready, reasons_loaded, device_bound;
static u32 console_size, xbl_size;
static int kelog_slot;
int g_decrypt_flg;
EXPORT_SYMBOL_GPL(g_decrypt_flg);

static int read_partition(void *buf, loff_t offset, size_t size)
{ return sd_partition_rw(RSTINFO_PARTITION, 0, offset, buf, size); }
static int write_partition(void *buf, loff_t offset, size_t size)
{ return sd_partition_rw(RSTINFO_PARTITION, 1, offset, buf, size); }
static bool header_layout_valid(struct rstinfo_header *head)
{
 return head->magic == RSTINFO_MAGIC && head->version == RSTINFO_VERSION &&
  head->blksize == 4096 && head->rstcnt_offset == RSTCNT_OFFSET &&
  head->rstcnt_size == RSTCNT_SIZE && head->rstlog_offset == RSTLOG_OFFSET &&
  head->rstlog_size == RSTLOG_SIZE && head->rstssn_offset == RSTSSN_OFFSET &&
  head->rstssn_size == RSTSSN_SIZE && head->rstssr_offset == RSTSSR_OFFSET &&
  head->rstssr_size == RSTSSR_SIZE;
}
static int validate_indices(struct rstinfo_header *head)
{
 int i;
 for (i = 0; i < 7; i++)
  if (head->rstcnt_head.index[i] >= 16)
   return -EUCLEAN;
 if (head->rstlog_head.index >= 16 || head->rstlog_head.logsum > 16)
  return -EUCLEAN;
 for (i = 0; i < 16; i++)
  if (head->rstlog_head.length[i] > RSTLOG_ENTITY_SIZE ||
      head->rstlog_head.type[i] >= 7 ||
      (head->rstlog_head.rstcnt_inedx[i] >= 16 &&
       head->rstlog_head.rstcnt_inedx[i] != U32_MAX))
   return -EUCLEAN;
 return 0;
}
static int load_header(void)
{
 int ret = read_partition(&rstinfo_head, 0, sizeof(rstinfo_head));
 if (ret)
  return ret;
 if (!header_layout_valid(&rstinfo_head)) {
  /* Stock preserves counters on an otherwise recognizable magic header. */
  if (rstinfo_head.magic != RSTINFO_MAGIC)
   memset(&rstinfo_head, 0, sizeof(rstinfo_head));
  rstinfo_head.magic = RSTINFO_MAGIC;
  rstinfo_head.version = RSTINFO_VERSION;
  rstinfo_head.blksize = 4096;
  rstinfo_head.rstcnt_offset = RSTCNT_OFFSET;
  rstinfo_head.rstcnt_size = RSTCNT_SIZE;
  rstinfo_head.rstlog_offset = RSTLOG_OFFSET;
  rstinfo_head.rstlog_size = RSTLOG_SIZE;
  rstinfo_head.rstssn_offset = RSTSSN_OFFSET;
  rstinfo_head.rstssn_size = RSTSSN_SIZE;
  rstinfo_head.rstssr_offset = RSTSSR_OFFSET;
  rstinfo_head.rstssr_size = RSTSSR_SIZE;
  ret = validate_indices(&rstinfo_head);
  if (ret)
   return ret;
  return write_partition(&rstinfo_head, 0, sizeof(rstinfo_head));
 }
 return validate_indices(&rstinfo_head);
}
static int rstinfo_update_rstcnt(u8 type, u8 subtype)
{
 struct rstcnt_entity entity = { };
 struct rtc_time tm;
 struct rstcnt_header next = rstinfo_head.rstcnt_head;
 int ret;
 u32 index;
 if (type >= 7)
  return -EINVAL;
 index = next.index[type];
 if (index >= 16)
  return -EUCLEAN;
 next.rstcnt[type]++;
 if (type >= 2) {
  entity.type = type;
  entity.subtype = subtype;
  entity.index = index;
  entity.time = ktime_get_real_seconds();
  strscpy(entity.linux_ver, init_utsname()->release, sizeof(entity.linux_ver));
  /* Original rodata+0x2da is this literal, not a recovery placeholder. */
  strscpy(entity.android_ver, "Flyme x.x.x.x", sizeof(entity.android_ver));
  strscpy(entity.build_time, init_utsname()->version, sizeof(entity.build_time));
  rtc_time64_to_tm(entity.time, &tm);
  if (tm.tm_year + 1900 <= 1979)
   entity.time += rstinfo_head.delt;
  ret = write_partition(&entity,
    RSTCNT_OFFSET + (loff_t)type * 65536 + (loff_t)index * 4096,
    sizeof(entity));
  if (ret)
   return ret;
  next.index[type] = (index + 1) & 15;
 }
 rstinfo_head.rstcnt_head = next;
 return write_partition(&rstinfo_head, 0, sizeof(rstinfo_head));
}
static void debugfs_encrypt_func(struct timer_list *timer)
{ WRITE_ONCE(g_decrypt_flg, 0); }
static int rstinfo_encrypt_erase(const char *command)
{
 struct rtc_time tm;
 u64 number;
 int sum, ret;
 if (READ_ONCE(g_decrypt_flg)) {
  if (!strcmp(command, "erase\n")) {
   if (!rstinfo_ready)
    return -EAGAIN;
   memset(&rstinfo_head, 0, sizeof(rstinfo_head));
   return write_partition(&rstinfo_head, 0, sizeof(rstinfo_head));
  }
  if (!strcmp(command, "encrypt\n"))
   WRITE_ONCE(g_decrypt_flg, 0);
  return 0;
 }
 ret = kstrtoull(command, 10, &number);
 if (ret)
  return ret;
 rtc_time64_to_tm(ktime_get_real_seconds() - (s32)(sys_tz.tz_minuteswest * 60), &tm);
 sum = tm.tm_min + tm.tm_hour;
 if (number >= max(sum - 24, 0) && number <= sum + 24)
  WRITE_ONCE(g_decrypt_flg, 1);
 mod_timer(&encrypt_timer, jiffies + 100 * HZ);
 return 0;
}
static void rstinfo_rstcnt_entity_print(struct seq_file *seq, struct rstcnt_entity *entity)
{
 struct rtc_time tm;
 if (entity->type >= ARRAY_SIZE(reset_reason_str))
  return;
 rtc_time64_to_tm(entity->time, &tm);
 seq_printf(seq, "| %u: %s %u-%u | ", entity->index,
   reset_reason_str[entity->type], entity->type, entity->subtype);
 seq_printf(seq, "%.*s | %.*s | %.*s | ", 64, entity->linux_ver,
   64, entity->android_ver, 64, entity->build_time);
 seq_printf(seq, "UTC time %d-%d-%d %d:%d:%d |\n", tm.tm_year + 1900,
   tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}
static void rstinfo_rstcnt_header_print(struct seq_file *seq)
{
 u64 sum = 0;
 int i;
 seq_puts(seq, "------------------------\n");
 for (i = 2; i <= 4; i++) {
  seq_printf(seq, "[   %-22s   ]\t(%llu)\n", reset_reason_str[i],
    rstinfo_head.rstcnt_head.rstcnt[i]);
  sum += rstinfo_head.rstcnt_head.rstcnt[i];
 }
 seq_printf(seq, "exception reboot cnt: %llu\n", sum);
 seq_puts(seq, "-----\n");
}
static int rstinfo_rstcnt_dump_type(struct seq_file *seq, u8 type)
{
 struct rstcnt_entity entity;
 unsigned int count, next, i;
 int ret;
 if (type >= 7)
  return -EINVAL;
 count = min_t(u32, rstinfo_head.rstcnt_head.rstcnt[type], 16);
 next = rstinfo_head.rstcnt_head.index[type];
 if (next >= 16)
  return -EUCLEAN;
 for (i = 0; i < count; i++) {
  next = (next + 15) & 15;
  ret = read_partition(&entity,
    RSTCNT_OFFSET + (loff_t)type * 65536 + (loff_t)next * 4096,
    sizeof(entity));
  if (ret) {
   seq_puts(seq, "read rstcnt_entity fail!\n");
   return ret;
  }
  rstinfo_rstcnt_entity_print(seq, &entity);
 }
 return 0;
}
static int rstinfo_cnt_debug_show(struct seq_file *seq, void *unused)
{
 if (!READ_ONCE(g_decrypt_flg)) {
  seq_puts(seq, "content is encrypt\n");
  return 0;
 }
 mutex_lock(&rstinfo_lock);
 rstinfo_rstcnt_header_print(seq);
 mutex_unlock(&rstinfo_lock);
 return 0;
}
static int rstinfo_cntlog_debug_show(struct seq_file *seq, void *unused)
{
 int i, ret = 0;
 if (!READ_ONCE(g_decrypt_flg)) {
  seq_puts(seq, "content is encrypt\n");
  return 0;
 }
 mutex_lock(&rstinfo_lock);
 rstinfo_rstcnt_header_print(seq);
 for (i = 2; i < 7 && !ret; i++)
  ret = rstinfo_rstcnt_dump_type(seq, i);
 mutex_unlock(&rstinfo_lock);
 return ret;
}
static int rstinfo_cnt_android_debug_show(struct seq_file *seq, void *unused)
{
 unsigned long type = (unsigned long)seq->private;
 int ret;
 if (!READ_ONCE(g_decrypt_flg)) {
  seq_puts(seq, "content is encrypt\n");
  return 0;
 }
 if (type >= 7)
  return -EINVAL;
 mutex_lock(&rstinfo_lock);
 seq_printf(seq, "%llu\n-------------\n%s: %llu\n\n",
   rstinfo_head.rstcnt_head.rstcnt[type], reset_reason_str[type],
   rstinfo_head.rstcnt_head.rstcnt[type]);
 ret = rstinfo_rstcnt_dump_type(seq, type);
 mutex_unlock(&rstinfo_lock);
 return ret;
}
static ssize_t rstinfo_cnt_debug_write(struct file *file, const char __user *buffer,
          size_t count, loff_t *offset)
{
 char command[64];
 size_t size = min(count, sizeof(command) - 1);
 int ret;
 if (copy_from_user(command, buffer, size))
  return -EFAULT;
 command[size] = '\0';
 mutex_lock(&rstinfo_lock);
 ret = rstinfo_encrypt_erase(command);
 mutex_unlock(&rstinfo_lock);
 return ret == -EAGAIN ? ret : size;
}
static ssize_t rstinfo_android_root_debug_write(struct file *file,
 const char __user *buffer, size_t count, loff_t *offset)
{
 char command[17] = { };
 int ret = 0;
 if (copy_from_user(command, buffer, min(count, sizeof(command) - 1)))
  return -EFAULT;
 if (!strncmp(command, "root", 4)) {
  mutex_lock(&rstinfo_lock);
  ret = rstinfo_ready ? rstinfo_update_rstcnt(6, 0) : -EAGAIN;
  mutex_unlock(&rstinfo_lock);
 }
 return ret ? ret : count;
}
static ssize_t rstinfo_android_rst_debug_write(struct file *file,
 const char __user *buffer, size_t count, loff_t *offset)
{
 char command[17] = { }, digits[4] = { };
 unsigned int subtype = 0, i;
 int ret = 0;
 if (copy_from_user(command, buffer, min(count, sizeof(command) - 1)))
  return -EFAULT;
 if (!strncmp(command, "reboot", 6)) {
  memcpy(digits, command + 7, 3);
  for (i = 0; i < 3 && digits[i] >= '0' && digits[i] <= '9'; i++)
   subtype = subtype * 10 + digits[i] - '0';
  mutex_lock(&rstinfo_lock);
  ret = rstinfo_ready ? rstinfo_update_rstcnt(5, subtype) : -EAGAIN;
  mutex_unlock(&rstinfo_lock);
 }
 return ret ? ret : count;
}
static ssize_t rstinfo_kelog_write(struct file *file, const char __user *buffer,
          size_t count, loff_t *offset)
{
 char command[64];
 size_t size = min(count, sizeof(command) - 1);
 u64 slot;
 int ret;
 if (copy_from_user(command, buffer, size))
  return -EFAULT;
 command[size] = '\0';
 ret = kstrtoull(command, 10, &slot);
 if (ret)
  return ret; /* Original forgot to unlock its mutex on this failure. */
 mutex_lock(&rstinfo_lock);
 kelog_slot = (u32)slot;
 mutex_unlock(&rstinfo_lock);
 return size;
}
static int print_xbl_log_and_console(struct seq_file *seq, void *buffer, size_t length)
{
 struct persistent_ram_buffer *log = buffer;
 size_t start, size, xbl_length;
 int ret;
 if (length < sizeof(*log) || log->sig != 0x43474244 ||
     log->start < 0 || log->size < 1)
  return 0;
 start = log->start;
 size = log->size;
 if (size < start || size >= console_size ||
     size > length - sizeof(*log) || console_size > length ||
     xbl_size > length - console_size)
  return -EUCLEAN;
 seq_puts(seq, "xbl log:\n");
 xbl_length = xbl_size > 1 ? strnlen((char *)buffer + console_size, xbl_size - 1) : 0;
 ret = seq_write(seq, (char *)buffer + console_size, xbl_length);
 if (ret)
  return ret;
 seq_puts(seq, "kernel log:\n");
 ret = seq_write(seq, log->data + start, size - start);
 if (!ret)
  ret = seq_write(seq, log->data, start);
 seq_puts(seq, "\n");
 return ret;
}
static int rstinfo_last_rstlog_show(struct seq_file *seq, void *unused)
{
 void *buffer;
 int ret;
 /* The original last_rstlog interface is independent of the timed proc gate. */
 if (rstinfo.reason < 2 || rstinfo.reason > 4)
  return 0;
 buffer = vzalloc(RSTLOG_ENTITY_SIZE);
 if (!buffer)
  return -ENOMEM;
 ret = read_partition(buffer, CURRENT_LOG_OFFSET, RSTLOG_ENTITY_SIZE);
 if (!ret) {
  seq_puts(seq, "------------------------\n");
  print_rstssr_times(seq);
  seq_puts(seq, "------------------------\n");
  ret = print_xbl_log_and_console(seq, buffer, RSTLOG_ENTITY_SIZE);
 }
 vfree(buffer);
 return ret;
}
static int rstinfo_kelog_show(struct seq_file *seq, void *unused)
{
 struct rstcnt_entity entity;
 struct rstlog_header *head = &rstinfo_head.rstlog_head;
 void *buffer;
 unsigned int type, index, length;
 int slot, ret = 0;
 if (!READ_ONCE(g_decrypt_flg)) {
  seq_puts(seq, "content is encrypt\n");
  return 0;
 }
 mutex_lock(&rstinfo_lock);
 slot = kelog_slot;
 if (slot < 0 || slot >= 16) {
  seq_printf(seq, "%d slot is invalid.\n", slot);
  ret = -EINVAL;
  goto out;
 }
 length = head->length[slot];
 type = head->type[slot];
 index = head->rstcnt_inedx[slot];
 /* Stock writes index-1 without masking when its counter ring wraps. */
 if (index == U32_MAX)
  index = 15;
 if (!length || length > RSTLOG_ENTITY_SIZE || type >= 7 || index >= 16) {
  seq_printf(seq, "rstlog in slot %d is invalid ret %d, size %ld, skip\n",
    slot, 0, (long)length);
  goto out;
 }
 ret = read_partition(&entity,
    RSTCNT_OFFSET + (loff_t)type * 65536 + (loff_t)index * 4096, sizeof(entity));
 if (ret) {
  seq_puts(seq, "read rstcnt_entity fail!\n");
  goto out;
 }
 rstinfo_rstcnt_entity_print(seq, &entity);
 seq_puts(seq, "\n");
 buffer = vzalloc(length);
 if (!buffer) { ret = -ENOMEM; goto out; }
 ret = read_partition(buffer, RSTLOG_OFFSET + (loff_t)slot * RSTLOG_ENTITY_SIZE, length);
 if (!ret)
  ret = print_xbl_log_and_console(seq, buffer, length);
 vfree(buffer);
out:
 mutex_unlock(&rstinfo_lock);
 return ret;
}
static int rstinfo_rstlog_debug_show(struct seq_file *seq, void *unused)
{
 struct rstlog_header *head = &rstinfo_head.rstlog_head;
 unsigned int count, i;
 if (!READ_ONCE(g_decrypt_flg)) {
  seq_puts(seq, "content is encrypt\n");
  return 0;
 }
 mutex_lock(&rstinfo_lock);
 count = min(head->logsum, 16U);
 if (count) {
  seq_puts(seq, "------------------------\n");
  seq_printf(seq, "recent: %d\n", (int)head->index - 1);
  seq_printf(seq, "entity_max_size: 0x%x\n", RSTLOG_ENTITY_SIZE);
  seq_printf(seq, "logsum: %llu\n", (u64)head->logsum);
  seq_puts(seq, "------------------------\n");
  for (i = 0; i < count; i++) {
   if (head->type[i] < 7)
    seq_printf(seq, "| %u: %s |\n", i, reset_reason_str[head->type[i]]);
  }
  seq_puts(seq, "------------------------\n");
 }
 mutex_unlock(&rstinfo_lock);
 return 0;
}
static int capture_current_log(void)
{
 struct rstlog_header *next = &rstinfo_head.rstlog_head;
 void *buffer;
 struct persistent_ram_buffer *log;
 u32 slot = next->index, type = rstinfo.reason;
 int ret;
 if (type < 2 || type > 4)
  return 0;
 if (slot >= 16)
  return -EUCLEAN;
 buffer = vzalloc(RSTLOG_ENTITY_SIZE);
 if (!buffer)
  return -ENOMEM;
 ret = read_partition(buffer, CURRENT_LOG_OFFSET, RSTLOG_ENTITY_SIZE);
 if (ret)
  goto free;
 log = buffer;
 if (!log->size)
  goto free;
 if (log->size < 0 || log->start < 0 || log->start > log->size ||
     log->size > RSTLOG_ENTITY_SIZE - sizeof(*log)) {
  ret = -EUCLEAN;
  goto free;
 }
 ret = write_partition(buffer,
   RSTLOG_OFFSET + (loff_t)slot * RSTLOG_ENTITY_SIZE, RSTLOG_ENTITY_SIZE);
 if (ret)
  goto free;
 next->length[slot] = RSTLOG_ENTITY_SIZE;
 next->type[slot] = type;
 next->rstcnt_inedx[slot] = (rstinfo_head.rstcnt_head.index[type] + 15) & 15;
 next->rstinfo[slot] = rstinfo;
 next->index = (slot + 1) & 15;
 next->logsum = min(next->logsum + 1, 16U);
 ret = write_partition(&rstinfo_head, 0, sizeof(rstinfo_head));
free:
 vfree(buffer);
 return ret;
}
static void reschedule_rstinfo(unsigned long delay)
{
 if (!READ_ONCE(stopping))
  schedule_delayed_work(&rstinfo_work, delay);
}
static void mz_rstinfo_work(struct work_struct *work)
{
 struct rtc_device *rtc;
 struct rtc_time tm;
 time64_t now, delta;
 int ret;
 if (READ_ONCE(stopping))
  return;
 mutex_lock(&rstinfo_lock);
 if (!rstinfo_ready) {
  if (!reasons_loaded) {
   ret = rstinfo_msm_init();
   if (ret)
    goto retry;
   reasons_loaded = true;
  }
  ret = load_header();
  if (ret)
   goto retry;
  ret = rstinfo_update_rstcnt(rstinfo.reason, 0);
  if (ret)
   goto retry;
  ret = capture_current_log();
  if (ret)
   pr_err("rstinfo: boot log recording failed: %d\n", ret);
  rstinfo_ready = true;
  goto retry;
 }
 now = ktime_get_real_seconds();
 rtc_time64_to_tm(now, &tm);
 if (tm.tm_year + 1900 <= 1970) {
  mutex_unlock(&rstinfo_lock);
  reschedule_rstinfo(10 * HZ);
  return;
 }
 rtc = rtc_class_open("rtc0");
 if (!rtc)
  goto retry;
 ret = rtc_read_time(rtc, &tm);
 rtc_class_close(rtc);
 if (ret)
  goto retry;
 delta = now - rtc_tm_to_time64(&tm);
 if (delta > 0 && delta != rstinfo_head.delt) {
  rstinfo_head.delt = delta;
  ret = write_partition(&rstinfo_head, 0, sizeof(rstinfo_head));
  if (ret)
   pr_err("rstinfo: time delta recording failed: %d\n", ret);
 }
retry:
 mutex_unlock(&rstinfo_lock);
 reschedule_rstinfo(HZ);
}
#define RST_PROC(name, show, write) \
static int name##_open(struct inode *inode, struct file *file) \
{ return single_open(file, show, PDE_DATA(inode)); } \
static const struct proc_ops name##_ops = { \
 .proc_open = name##_open, .proc_read = seq_read, .proc_lseek = seq_lseek, \
 .proc_release = single_release, .proc_write = write }
RST_PROC(cnt, rstinfo_cnt_debug_show, rstinfo_cnt_debug_write);
RST_PROC(cntlog, rstinfo_cntlog_debug_show, NULL);
RST_PROC(rstlog, rstinfo_rstlog_debug_show, NULL);
RST_PROC(android, rstinfo_cnt_android_debug_show, rstinfo_android_rst_debug_write);
RST_PROC(root, rstinfo_cnt_android_debug_show, rstinfo_android_root_debug_write);
static int last_open(struct inode *inode, struct file *file)
{ return single_open_size(file, rstinfo_last_rstlog_show, PDE_DATA(inode), RSTLOG_ENTITY_SIZE); }
static const struct proc_ops last_ops = {
 .proc_open = last_open, .proc_read = seq_read, .proc_lseek = seq_lseek,
 .proc_release = single_release,
};
static int kelog_open(struct inode *inode, struct file *file)
{ return single_open_size(file, rstinfo_kelog_show, PDE_DATA(inode), RSTLOG_ENTITY_SIZE); }
static const struct proc_ops kelog_ops = {
 .proc_open = kelog_open, .proc_read = seq_read, .proc_lseek = seq_lseek,
 .proc_release = single_release, .proc_write = rstinfo_kelog_write,
};
static ssize_t rstinfo_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
          const char *buffer, size_t count)
{
 int ret;
 mutex_lock(&rstinfo_lock);
 ret = rstinfo_encrypt_erase(buffer);
 mutex_unlock(&rstinfo_lock);
 return ret == -EAGAIN ? ret : count;
}
static struct kobj_attribute rstinfo_enable_attr = __ATTR(rstinfo_enable, 0220,
                NULL, rstinfo_enable_store);
static void remove_interfaces(void)
{
 int i;
 if (rstinfo_kobj) {
  sysfs_remove_file(rstinfo_kobj, &rstinfo_enable_attr.attr);
  kobject_put(rstinfo_kobj);
  rstinfo_kobj = NULL;
 }
 for (i = 0; i < ARRAY_SIZE(proc_nodes); i++) {
  proc_remove(proc_nodes[i]);
  proc_nodes[i] = NULL;
 }
}
static int mz_rstinfo_probe(struct platform_device *pdev)
{
 static const struct {
  const char *name;
  umode_t mode;
  const struct proc_ops *ops;
  unsigned long type;
 } files[] = {
  { "rstinfo_cnt", 0664, &cnt_ops },
  { "rstinfo_cnt_log", 0444, &cntlog_ops },
  { "rstinfo_rst_log", 0444, &rstlog_ops },
  { "rstinfo_android_cnt", 0664, &android_ops, 5 },
  { "rootinfo_cnt", 0664, &root_ops, 6 },
  { "last_rstlog", 0444, &last_ops },
  { "kelog", 0664, &kelog_ops },
 };
 struct proc_dir_entry *stability;
 int ret, i;
 char path[64];
 mutex_lock(&registry_lock);
 if (device_bound) { ret = -EBUSY; goto unlock; }
 console_size = 0;
 xbl_size = 0;
 of_property_read_u32(pdev->dev.of_node, "console-size", &console_size);
 of_property_read_u32(pdev->dev.of_node, "xbl-log-size", &xbl_size);
 if (console_size > RSTLOG_ENTITY_SIZE || xbl_size > RSTLOG_ENTITY_SIZE - console_size) {
  ret = -EINVAL;
  goto unlock;
 }
 WRITE_ONCE(stopping, false);
 rstinfo_ready = false;
 reasons_loaded = false;
 timer_setup(&encrypt_timer, debugfs_encrypt_func, 0);
 INIT_DELAYED_WORK(&rstinfo_work, mz_rstinfo_work);
 rstinfo_kobj = kobject_create_and_add("rstinfo", NULL);
 if (!rstinfo_kobj) { ret = -ENOMEM; goto unlock; }
 ret = sysfs_create_file(rstinfo_kobj, &rstinfo_enable_attr.attr);
 if (ret)
  goto remove;
 /* /proc/stability can be shared. Own only our leaves, never delete the root. */
 stability = proc_mkdir("stability", NULL);
 for (i = 0; i < ARRAY_SIZE(files); i++) {
  if (stability)
   proc_nodes[i] = proc_create_data(files[i].name, files[i].mode, stability,
      files[i].ops, (void *)files[i].type);
  else {
   snprintf(path, sizeof(path), "stability/%s", files[i].name);
   proc_nodes[i] = proc_create_data(path, files[i].mode, NULL,
      files[i].ops, (void *)files[i].type);
  }
  if (!proc_nodes[i]) { ret = -ENOMEM; goto remove; }
 }
 /* This module's three SSR leaves have the same shared-root ownership. */
 if (!stability) {
  /* proc_create accepts a relative full path when parent is NULL; the SSR
   * helper uses the same convention for an already-existing stability root. */
  ret = rstinfo_ssr_init(NULL);
 } else
  ret = rstinfo_ssr_init(stability);
 if (ret)
  goto remove;
 device_bound = true;
 schedule_delayed_work(&rstinfo_work, 2 * HZ);
 mutex_unlock(&registry_lock);
 return 0;
remove:
 WRITE_ONCE(stopping, true);
 remove_interfaces();
 cancel_delayed_work_sync(&rstinfo_work);
 del_timer_sync(&encrypt_timer);
 rstinfo_reason_exit();
unlock:
 mutex_unlock(&registry_lock);
 return ret;
}
static int mz_rstinfo_remove(struct platform_device *pdev)
{
 mutex_lock(&registry_lock);
 WRITE_ONCE(stopping, true);
 rstinfo_ssr_exit();
 remove_interfaces();
 cancel_delayed_work_sync(&rstinfo_work);
 del_timer_sync(&encrypt_timer);
 rstinfo_reason_exit();
 WRITE_ONCE(g_decrypt_flg, 0);
 device_bound = false;
 mutex_unlock(&registry_lock);
 return 0;
}
static const struct of_device_id mz_rstinfo_of_match[] = {
 { .compatible = "mz_rstinfo" }, { }
};
MODULE_DEVICE_TABLE(of, mz_rstinfo_of_match);
static struct platform_driver mz_rstinfo_driver = {
 .probe = mz_rstinfo_probe,
 .remove = mz_rstinfo_remove,
 .driver = { .name = "mz_rstinfo", .of_match_table = mz_rstinfo_of_match },
};
module_platform_driver(mz_rstinfo_driver);
MODULE_DESCRIPTION("M2391 persistent reset and subsystem crash information");
MODULE_LICENSE("GPL");
