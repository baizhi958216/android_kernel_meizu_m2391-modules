// SPDX-License-Identifier: GPL-2.0-only
/* Boot reset records: original .text+0x22d0..0x3440 and split BTF. */
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include "../storage/meizu_storage.h"
#include "mz_rstinfo_internal.h"
#include "mz_rstinfo_strings.h"
struct reset_info rstinfo;
static struct proc_dir_entry *reset_reason_proc;
static const char *reason_bit(u32 bits, unsigned int first,
        const char * const *table, unsigned int count)
{
 unsigned int i;
 for (i = first; i < count; i++)
  if (bits & BIT(i))
   return table[i];
 return "N/A";
}
static void print_ocp_info(struct reset_info *info, struct seq_file *seq)
{
 unsigned int i;
 bool found = false;
 for (i = 0; i < ARRAY_SIZE(info->ocpinfo.item); i++) {
  struct ocp_item *item = &info->ocpinfo.item[i];
  if (!item->ocp_occurred || item->pmic_index >= ARRAY_SIZE(sm8550_pmic_str))
   continue;
  if (seq)
   seq_printf(seq, "OCP occurred: pmic_id: %d %s, smps:%d, ldo:%d, bob:%d\n",
    item->pmic_index, sm8550_pmic_str[item->pmic_index],
    item->smps_count, item->ldo_count, item->bob_count);
  else
   pr_info("rstinfo: OCP occurred: pmic_id: %d %s, smps:%d, ldo:%d, bob:%d\n",
    item->pmic_index, sm8550_pmic_str[item->pmic_index],
    item->smps_count, item->ldo_count, item->bob_count);
  found = true;
 }
 if (!found) {
  if (seq)
   seq_puts(seq, "OCP occurred: N/A\n");
  else
   pr_info("rstinfo: OCP occurred: N/A\n");
 }
}
static void rstinfo_print_reason(struct reset_info *info)
{
 pr_info("rstinfo: %s, restart reason=%d\n",
   info->backed_rstinfo == 0x55 ? "BACKED_RSTINFO" : "CURRENT_RSTINFO",
   info->restart_reason);
 pr_info("rstinfo: ON SEQ: %s\n", reason_bit(info->on_reason, 0,
   qpnp_on_seq_reason, ARRAY_SIZE(qpnp_on_seq_reason)));
 pr_info("rstinfo: OFF SEQ: %s\n", reason_bit(info->off_reason, 0,
   qpnp_off_seq_reason, ARRAY_SIZE(qpnp_off_seq_reason)));
 pr_info("rstinfo: PON: %s\n", reason_bit(info->pon_reason, 0,
   qpnp_pon_reason, ARRAY_SIZE(qpnp_pon_reason)));
 pr_info("rstinfo: POFF: %s\n", reason_bit(info->poff_reason, 0,
   qpnp_poff_warm_reason, ARRAY_SIZE(qpnp_poff_warm_reason)));
 pr_info("rstinfo: WARM: %s\n", reason_bit(info->warm_reason, 0,
   qpnp_poff_warm_reason, ARRAY_SIZE(qpnp_poff_warm_reason)));
 pr_info("rstinfo: FAULT: %s\n", reason_bit(info->fault_reason, 0,
   qpnp_fault_reason, ARRAY_SIZE(qpnp_fault_reason)));
 pr_info("rstinfo: S3 Reset: %s\n", reason_bit(info->off_reason, 5,
   qpnp_off_seq_reason, ARRAY_SIZE(qpnp_off_seq_reason)));
 print_ocp_info(info, NULL);
 pr_info("rstinfo: Pon Log Event:\n");
 pr_info("rstinfo: %.*s", (int)sizeof(info->pon_log), info->pon_log);
}
void rstinfo_reason_show(struct seq_file *seq, struct reset_info *info)
{
 seq_printf(seq, "rstinfo_reason_show: %s\n",
   info->backed_rstinfo == 0x55 ? "BACKED_RSTINFO" : "CURRENT_RSTINFO");
 if (info->reason < ARRAY_SIZE(reset_reason_str))
  seq_printf(seq, "\nReset Reason Type: %s\n\n", reset_reason_str[info->reason]);
 seq_printf(seq, "GCC_RESET_STATUS: 0x%08X\n", info->gcc_reset_status);
 if (info->backed_rstinfo == 0x55 && info->reason != 4)
  seq_printf(seq, "Restart Reason: 0x%02X\n", info->restart_reason);
 seq_printf(seq, "PON Seq Reason: %s\n",
   reason_bit(info->on_reason, 0, qpnp_on_seq_reason, ARRAY_SIZE(qpnp_on_seq_reason)));
 seq_printf(seq, "POFF Seq Reason: %s\n",
   reason_bit(info->off_reason, 0, qpnp_off_seq_reason, ARRAY_SIZE(qpnp_off_seq_reason)));
 seq_printf(seq, "PON Reason: %s\n",
   reason_bit(info->pon_reason, 0, qpnp_pon_reason, ARRAY_SIZE(qpnp_pon_reason)));
 seq_printf(seq, "POFF Reason: %s\n",
   reason_bit(info->poff_reason, 0, qpnp_poff_warm_reason, ARRAY_SIZE(qpnp_poff_warm_reason)));
 seq_printf(seq, "WARM Reason: %s\n",
   reason_bit(info->warm_reason, 0, qpnp_poff_warm_reason, ARRAY_SIZE(qpnp_poff_warm_reason)));
 seq_printf(seq, "FAULT Reason: %s\n",
   reason_bit(info->fault_reason, 0, qpnp_fault_reason, ARRAY_SIZE(qpnp_fault_reason)));
 /* Original S3 helper decodes off_reason bits5..10, not s3_reset_reason. */
 seq_printf(seq, "S3 Reset Reason: %s\n",
   reason_bit(info->off_reason, 5, qpnp_off_seq_reason, ARRAY_SIZE(qpnp_off_seq_reason)));
 print_ocp_info(info, seq);
 seq_puts(seq, "Pon Log Event:\n");
 seq_printf(seq, "%.*s", (int)sizeof(info->pon_log), info->pon_log);
}
static int rstinfo_reset_reason_show(struct seq_file *seq, void *unused)
{
 rstinfo_reason_show(seq, &rstinfo);
 seq_printf(seq, "\nANDROID_VERSION: %s\n", "Flyme x.x.x.x");
 seq_printf(seq, "LINUX_VERSION: %s\n", init_utsname()->release);
 seq_printf(seq, "BUILD_TIME: %s\n", init_utsname()->version);
 return 0;
}
static int rstinfo_reset_reason_open(struct inode *inode, struct file *file)
{ return single_open(file, rstinfo_reset_reason_show, PDE_DATA(inode)); }
static const struct proc_ops reset_reason_ops = {
 .proc_open = rstinfo_reset_reason_open, .proc_read = seq_read,
 .proc_lseek = seq_lseek, .proc_release = single_release,
};
int rstinfo_msm_init(void)
{
 struct reset_info *zero;
 int ret;
 ret = sd_partition_rw(RSTINFO_PARTITION, 0, 4096, &rstinfo, sizeof(rstinfo));
 if (ret)
  return ret;
 if (rstinfo.backed_rstinfo == 0x55) {
  rstinfo_print_reason(&rstinfo);
  if (rstinfo.restart_reason == 27)
   rstinfo.reason = 2;
  else if (rstinfo.restart_reason == 29)
   rstinfo.reason = 4;
  else
   rstinfo.reason = rstinfo.gcc_reset_status ? 3 : 1;
 } else {
  ret = sd_partition_rw(RSTINFO_PARTITION, 0, 8192, &rstinfo, sizeof(rstinfo));
  if (ret)
   return ret;
  rstinfo.reason = 1;
  if (rstinfo.backed_rstinfo == 0xaa)
   rstinfo_print_reason(&rstinfo);
  if (rstinfo.backed_rstinfo == 0xaa &&
      !(rstinfo.poff_reason & 0xe0) && !(rstinfo.warm_reason & 0xe0) &&
      (rstinfo.fault_reason & 0x90f0))
   rstinfo.reason = 4;
 }
 if (!reset_reason_proc) {
  reset_reason_proc = proc_create("reset_reason", 0444, NULL, &reset_reason_ops);
  if (!reset_reason_proc)
   return -ENOMEM;
 }
 if (rstinfo.backed_rstinfo != 0x55)
  return 0;
 zero = kzalloc(sizeof(*zero), GFP_KERNEL);
 if (!zero)
  return -ENOMEM;
 ret = sd_partition_rw(RSTINFO_PARTITION, 1, 4096, zero, sizeof(*zero));
 kfree(zero);
 return ret;
}
void rstinfo_reason_exit(void)
{
 proc_remove(reset_reason_proc);
 reset_reason_proc = NULL;
}
