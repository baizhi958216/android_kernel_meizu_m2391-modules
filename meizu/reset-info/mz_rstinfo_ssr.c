// SPDX-License-Identifier: GPL-2.0-only
/* Original SSR record ABI from split BTF and .text+0x347c..0x4380.
 * Each retained record and pending disk work owns a reference: trimming the
 * 1024-entry current list cannot free a queued/running writer as stock did.
 */
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/soc/qcom/q6v5_rstinfo.h>
#include "../storage/meizu_storage.h"
#include "mz_rstinfo_internal.h"
struct rstssr_node {
 struct rstssr_info info;
 struct list_head list;
 struct work_struct work;
 struct kref ref;
};
static LIST_HEAD(current_records);
static DEFINE_MUTEX(current_lock);
static DEFINE_MUTEX(partition_lock);
static struct workqueue_struct *ssr_queue;
static u64 current_crashes;
static unsigned int current_count;
static bool ssr_stopping;
static struct proc_dir_entry *ssr_proc[3];
static void ssr_free(struct kref *ref)
{
 struct rstssr_node *node = container_of(ref, struct rstssr_node, ref);
 kfree(node);
}
static int ssr_header_read(struct rstssr_header *head)
{
 int ret = sd_partition_rw(RSTINFO_PARTITION, 0, RSTSSR_HEAD_OFFSET,
          head, sizeof(*head));
 if (ret)
  return ret;
 if (head->record_count < 0 || head->record_count > 48 ||
     head->info_start < 0 || head->info_start > 1023 ||
     head->info_end < 0 || head->info_end > 1023)
  return -EUCLEAN;
 return 0;
}
static void rstssr_write_partition_handle_work(struct work_struct *work)
{
 struct rstssr_node *node = container_of(work, struct rstssr_node, work);
 struct rstssr_header *head;
 int ret, slot, i, sum;
 head = kzalloc(sizeof(*head), GFP_KERNEL);
 if (!head)
  goto put;
 mutex_lock(&partition_lock);
 ret = ssr_header_read(head);
 if (ret)
  goto unlock;
 sum = head->info_start + head->info_end;
 if (sum <= 1022)
  slot = head->info_end;
 else if (sum == 1023)
  slot = head->info_start;
 else
  goto unlock; /* Same original no-write condition for an inconsistent ring. */
 ret = sd_partition_rw(RSTINFO_PARTITION, 1,
   RSTSSR_DATA_OFFSET + (loff_t)slot * 1024,
   &node->info, sizeof(node->info));
 if (ret)
  goto unlock;
 if (sum <= 1022) {
  head->info_start = 0;
  if (++head->info_end == 1023) {
   head->info_start = 1023;
   head->info_end = 0;
  }
 } else {
  if (++head->info_start > 1023)
   head->info_start = 0;
  if (--head->info_end < 0)
   head->info_end = 1023;
 }
 for (i = 0; i < head->record_count; i++) {
  /* Bounded comparison handles corrupt/nonterminated on-disk names. */
  if (!strncmp(head->subsystem_name[i], node->info.subsystem_name, 64))
   break;
 }
 if (i == head->record_count) {
  if (i == 48)
   goto unlock;
  memcpy(head->subsystem_name[i], node->info.subsystem_name, 64);
  head->record_count++;
 }
 if (head->crash_times[i] != U64_MAX)
  head->crash_times[i]++;
 sd_partition_rw(RSTINFO_PARTITION, 1, RSTSSR_HEAD_OFFSET, head, sizeof(*head));
unlock:
 mutex_unlock(&partition_lock);
 kfree(head);
put:
 kref_put(&node->ref, ssr_free);
}
void get_ssrinfo(const char *ss_name, char *crash_type, char *msg, size_t msg_len)
{
 struct rstssr_node *node, *old;
 size_t length;
 if (!ss_name || !crash_type || !msg)
  return;
 node = kzalloc(sizeof(*node), GFP_KERNEL);
 if (!node)
  return;
 node->info.magic = RSTSSR_MAGIC;
 strscpy(node->info.subsystem_name, ss_name, sizeof(node->info.subsystem_name));
 strscpy(node->info.crash_type, crash_type, sizeof(node->info.crash_type));
 length = min(msg_len, sizeof(node->info.crash_info));
 memcpy(node->info.crash_info, msg, strnlen(msg, length));
 node->info.crash_time = ktime_get_real_seconds() - (s32)(sys_tz.tz_minuteswest * 60);
 kref_init(&node->ref);
 INIT_WORK(&node->work, rstssr_write_partition_handle_work);
 mutex_lock(&current_lock);
 if (ssr_stopping || !ssr_queue) {
  mutex_unlock(&current_lock);
  kref_put(&node->ref, ssr_free);
  return;
 }
 node->info.index = current_crashes++;
 if (current_count == 1024) {
  old = list_first_entry(&current_records, struct rstssr_node, list);
  list_del(&old->list);
  kref_put(&old->ref, ssr_free);
  current_count--;
 }
 list_add_tail(&node->list, &current_records);
 current_count++;
 kref_get(&node->ref);
 queue_work(ssr_queue, &node->work);
 mutex_unlock(&current_lock);
}
EXPORT_SYMBOL_GPL(get_ssrinfo);
static void show_record(struct seq_file *seq, struct rstssr_info *info, bool index)
{
 struct rtc_time tm;
 rtc_time64_to_tm(info->crash_time, &tm);
 if (index)
  seq_printf(seq, "[%d]", (int)info->index);
 seq_printf(seq, "[%d-%02d-%02d %02d:%02d:%02d] %.*s %.*s: %.*s\n",
   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
   tm.tm_hour, tm.tm_min, tm.tm_sec, 64, info->subsystem_name,
   64, info->crash_type, 512, info->crash_info);
}
static int rstssr_current_show(struct seq_file *seq, void *unused)
{
 struct rstssr_node *node;
 mutex_lock(&current_lock);
 list_for_each_entry(node, &current_records, list)
  if (node->info.magic == RSTSSR_MAGIC)
   show_record(seq, &node->info, true);
 mutex_unlock(&current_lock);
 return 0;
}
static void show_times(struct seq_file *seq, struct rstssr_header *head)
{
 int i;
 for (i = 0; i < head->record_count; i++)
  seq_printf(seq, "%.*s crash times: %ld\n", 64,
    head->subsystem_name[i], (long)head->crash_times[i]);
}
void print_rstssr_times(struct seq_file *seq)
{
 struct rstssr_header *head = kzalloc(sizeof(*head), GFP_KERNEL);
 if (!head)
  return;
 mutex_lock(&partition_lock);
 if (!ssr_header_read(head))
  show_times(seq, head);
 mutex_unlock(&partition_lock);
 kfree(head);
}
static int ssr_show(struct seq_file *seq, bool records)
{
 struct rstssr_header *head;
 struct rstssr_info *info = NULL;
 int ret, i, end, sum;
 if (!READ_ONCE(g_decrypt_flg)) {
  seq_puts(seq, "content is encrypt\n");
  return 0;
 }
 head = kzalloc(sizeof(*head), GFP_KERNEL);
 if (!head)
  return -ENOMEM;
 if (records) {
  info = kzalloc(sizeof(*info), GFP_KERNEL);
  if (!info) { kfree(head); return -ENOMEM; }
 }
 mutex_lock(&partition_lock);
 ret = ssr_header_read(head);
 if (ret)
  goto out;
 show_times(seq, head);
 if (!records)
  goto out;
 seq_puts(seq, "----------------------------------\n");
 sum = head->info_start + head->info_end;
 end = sum < 1023 ? head->info_end : 1024;
 for (i = head->info_start; i < end; i++) {
  ret = sd_partition_rw(RSTINFO_PARTITION, 0,
    RSTSSR_DATA_OFFSET + (loff_t)i * 1024, info, sizeof(*info));
  if (ret)
   goto out;
  if (info->magic == RSTSSR_MAGIC)
   show_record(seq, info, false);
 }
 if (sum >= 1023)
  for (i = 0; i < head->info_start; i++) {
   ret = sd_partition_rw(RSTINFO_PARTITION, 0,
     RSTSSR_DATA_OFFSET + (loff_t)i * 1024, info, sizeof(*info));
   if (ret)
    goto out;
   if (info->magic == RSTSSR_MAGIC)
    show_record(seq, info, false);
  }
out:
 mutex_unlock(&partition_lock);
 kfree(info);
 kfree(head);
 return ret;
}
static int rstssr_record_show(struct seq_file *seq, void *unused)
{ return ssr_show(seq, true); }
static int rstssr_times_show(struct seq_file *seq, void *unused)
{ return ssr_show(seq, false); }
static ssize_t rstssr_write(struct file *file, const char __user *buffer,
        size_t count, loff_t *offset)
{
 char command[64];
 void *zero;
 size_t size = min(count, sizeof(command) - 1);
 loff_t off;
 int ret;
 if (copy_from_user(command, buffer, size))
  return -EFAULT;
 command[size] = '\0';
 if (strcmp(command, "erase\n"))
  return -EFAULT;
 zero = kzalloc(4096, GFP_KERNEL);
 if (!zero)
  return -ENOMEM;
 mutex_lock(&partition_lock);
 ret = sd_partition_rw(RSTINFO_PARTITION, 1, RSTSSR_HEAD_OFFSET, zero, 4096);
 for (off = RSTSSR_DATA_OFFSET; !ret && off < RSTSSR_HEAD_OFFSET + 1052672;
      off += 1024)
  ret = sd_partition_rw(RSTINFO_PARTITION, 1, off, zero, 1024);
 mutex_unlock(&partition_lock);
 kfree(zero);
 return ret ? ret : size;
}
#define SSR_PROC(name) \
static int name##_open(struct inode *inode, struct file *file) \
{ return single_open(file, name##_show, PDE_DATA(inode)); } \
static const struct proc_ops name##_ops = { \
 .proc_open = name##_open, .proc_read = seq_read, .proc_lseek = seq_lseek, \
 .proc_release = single_release, .proc_write = rstssr_write }
SSR_PROC(rstssr_current);
SSR_PROC(rstssr_times);
SSR_PROC(rstssr_record);
int rstinfo_ssr_init(struct proc_dir_entry *dir)
{
 static const char * const names[] = { "rstssr_current", "rstssr_times", "rstssr_record" };
 static const struct proc_ops * const ops[] = {
  &rstssr_current_ops, &rstssr_times_ops, &rstssr_record_ops };
 int i, ret;
 char path[64];
 ssr_stopping = false;
 ssr_queue = alloc_ordered_workqueue("rstinfo-ssr", WQ_MEM_RECLAIM);
 if (!ssr_queue)
  return -ENOMEM;
 for (i = 0; i < ARRAY_SIZE(ssr_proc); i++) {
  if (dir)
   ssr_proc[i] = proc_create(names[i], 0664, dir, ops[i]);
  else {
   snprintf(path, sizeof(path), "stability/%s", names[i]);
   ssr_proc[i] = proc_create(path, 0664, NULL, ops[i]);
  }
  if (!ssr_proc[i]) { ret = -ENOMEM; goto fail; }
 }
 ret = qcom_q6v5_register_rstinfo(get_ssrinfo);
 if (!ret)
  return 0;
fail:
 rstinfo_ssr_exit();
 return ret;
}
void rstinfo_ssr_exit(void)
{
 struct rstssr_node *node, *next;
 int i;
 qcom_q6v5_unregister_rstinfo(get_ssrinfo);
 mutex_lock(&current_lock);
 ssr_stopping = true;
 mutex_unlock(&current_lock);
 for (i = 0; i < ARRAY_SIZE(ssr_proc); i++) {
  proc_remove(ssr_proc[i]);
  ssr_proc[i] = NULL;
 }
 if (ssr_queue)
  destroy_workqueue(ssr_queue);
 mutex_lock(&current_lock);
 ssr_queue = NULL;
 list_for_each_entry_safe(node, next, &current_records, list) {
  list_del(&node->list);
  kref_put(&node->ref, ssr_free);
 }
 current_count = 0;
 mutex_unlock(&current_lock);
}
