/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _M2391_RSTINFO_INTERNAL_H
#define _M2391_RSTINFO_INTERNAL_H
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include "mz_rstinfo_format.h"
#define RSTINFO_PARTITION "rstinfo"
#define RSTSSR_HEAD_OFFSET 0x873000
#define RSTSSR_DATA_OFFSET 0x874000
#define RSTSSR_MAGIC 0x53535243
struct seq_file;
extern struct reset_info rstinfo;
extern int g_decrypt_flg;
void print_rstssr_times(struct seq_file *);
void rstinfo_reason_show(struct seq_file *, struct reset_info *);
int rstinfo_msm_init(void);
void rstinfo_reason_exit(void);
int rstinfo_ssr_init(struct proc_dir_entry *dir);
void rstinfo_ssr_exit(void);
void get_ssrinfo(const char *ss_name, char *crash_type, char *msg, size_t msg_len);
#endif
