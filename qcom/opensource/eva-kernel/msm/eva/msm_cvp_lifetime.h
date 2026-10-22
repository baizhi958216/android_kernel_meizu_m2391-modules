/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSM_CVP_LIFETIME_H
#define MSM_CVP_LIFETIME_H
#include <linux/poll.h>
#include "msm_cvp_internal.h"
struct msm_cvp_core;
struct msm_cvp_inst;
struct eva_kmd_arg;
struct task_struct;
struct file;
void *msm_cvp_open(int core_id, int session_type, struct task_struct *task);
int msm_cvp_close(void *instance);
int msm_cvp_private(void *instance, unsigned int cmd, struct eva_kmd_arg *arg);
int msm_cvp_poll(void *instance, struct file *filp, struct poll_table_struct *wait);
int msm_cvp_suspend(int core_id);
long cvp_lifetime_ioctl(void *handle, unsigned int cmd, unsigned long arg);
unsigned int cvp_lifetime_poll(void *handle, struct file *file, poll_table *wait);
long cvp_instance_ioctl(struct msm_cvp_inst *inst, unsigned int cmd, unsigned long arg);
bool cvp_lifetime_stopping(void);
int cvp_lifetime_prepare(struct msm_cvp_core *core);
bool cvp_lifetime_schedule_ssr(struct msm_cvp_core *core,
		enum hal_ssr_trigger_type type);
void cvp_lifetime_publish(struct msm_cvp_core *core);
void cvp_lifetime_stop(struct msm_cvp_core *core);
void cvp_lifetime_drain(void);
void cvp_lifetime_debugfs_drain(struct msm_cvp_core *core);
void cvp_lifetime_detach(void);
void cvp_lifetime_unpublish(void);
#endif
