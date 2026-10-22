/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MSM_VIDC_LIFETIME_H_
#define _MSM_VIDC_LIFETIME_H_
#include <linux/srcu.h>
#include <linux/workqueue.h>
struct msm_vidc_core;
struct msm_vidc_inst;
extern struct srcu_struct vidc_operations;
extern bool vidc_ready;
void vidc_publish(struct msm_vidc_core *core);
void vidc_stop(struct msm_vidc_core *core);
void vidc_debugfs_drain(struct msm_vidc_core *core);
bool vidc_cleanup_ready(struct msm_vidc_core *core);
void vidc_core_put(struct msm_vidc_core *core);
void vidc_inst_cleanup(struct msm_vidc_inst *inst);
void vidc_poll_attach(struct msm_vidc_inst *inst);
void vidc_poll_detach(struct msm_vidc_inst *inst);
int vidc_hw_close(void *instance);
bool vidc_queue_delayed(struct msm_vidc_core *core, struct workqueue_struct *queue,
	struct delayed_work *work, unsigned long delay);
bool vidc_queue_work(struct msm_vidc_core *core, struct work_struct *work);
#endif
