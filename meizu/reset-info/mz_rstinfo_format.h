/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _M2391_RSTINFO_FORMAT_H
#define _M2391_RSTINFO_FORMAT_H
#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/time64.h>
/* Original split BTF: reset-info wire layouts. These objects are read/written
 * verbatim; retain original alignment and the misspelled rstcnt_inedx field.
 */
struct ocp_item {
 bool ocp_occurred;
 u8 pmic_index, smps_count, ldo_count, bob_count;
};
struct ocp_info { struct ocp_item item[20]; };
struct reset_info {
 u8 backed_rstinfo, restart_reason, reason;
 u32 warm_reason, pon_reason, poff_reason, gcc_reset_status;
 u32 s3_reset_reason, fault_reason, on_reason, off_reason;
 struct ocp_info ocpinfo;
 char pon_log[1024];
};
struct rstcnt_header {
 u32 index[7];
 u64 rstcnt[7];
};
struct rstlog_header {
 u32 index, logsum;
 u32 length[16], type[16], rstcnt_inedx[16];
 struct reset_info rstinfo[16];
};
struct rstinfo_header {
 u32 magic, version, blksize, rstcnt_offset, rstcnt_size;
 u32 rstlog_offset, rstlog_size;
 struct rstcnt_header rstcnt_head;
 struct rstlog_header rstlog_head;
 time64_t delt;
 u32 rstssn_offset, rstssn_size, rstssr_offset, rstssr_size;
};
struct rstcnt_entity {
 u8 type, subtype;
 u32 index;
 time64_t time;
 char linux_ver[64], android_ver[64], build_time[64];
};
struct rstssr_info {
 int magic;
 uint64_t index;
 char subsystem_name[64], crash_type[64];
 time64_t crash_time;
 char crash_info[512];
};
struct rstssr_header {
 int record_count;
 uint64_t crash_times[48];
 char subsystem_name[48][64];
 int info_start, info_end;
};
static_assert(sizeof(struct reset_info) == 1160);
static_assert(sizeof(struct rstcnt_header) == 88);
static_assert(sizeof(struct rstlog_header) == 18760);
static_assert(sizeof(struct rstinfo_header) == 18904);
static_assert(sizeof(struct rstcnt_entity) == 208);
static_assert(sizeof(struct rstssr_info) == 664);
static_assert(sizeof(struct rstssr_header) == 3472);
#endif
