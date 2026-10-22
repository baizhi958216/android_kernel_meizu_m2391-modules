/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CAM_FLASH_MEIZU_H
#define CAM_FLASH_MEIZU_H
#include "cam_flash_dev.h"
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
bool cam_flash_meizu_matches(struct cam_flash_ctrl *ctrl);
int cam_flash_meizu_apply_power(struct cam_flash_ctrl *ctrl, bool enable);
int cam_flash_meizu_prepare(struct cam_flash_ctrl *ctrl, bool enable);
int cam_flash_meizu_ops(struct cam_flash_ctrl *ctrl,
	struct cam_flash_frame_setting *data, enum camera_flash_opcode op);
int cam_flash_meizu_off(struct cam_flash_ctrl *ctrl);
/* Caller serializes these factory controls with the flash mutex. */
struct cam_flash_meizu_factory {
	bool powered;
	u32 channels;
};
int cam_flash_meizu_factory_channels(struct cam_flash_ctrl *ctrl,
	struct cam_flash_meizu_factory *state, int enable, int mode, int led, int duty);
int cam_flash_meizu_factory_all(struct cam_flash_ctrl *ctrl,
	bool *enabled, int duty);
struct cam_flash_meizu_pattern {
	struct cam_flash_ctrl *ctrl;
	struct work_struct work;
	struct hrtimer timer;
	bool running, powered, stopping;
	u32 index, saved_index, counter;
	s32 brightness;
};
void cam_flash_meizu_pattern_init(struct cam_flash_meizu_pattern *pattern,
	struct cam_flash_ctrl *ctrl);
void cam_flash_meizu_pattern_start(struct cam_flash_meizu_pattern *pattern);
void cam_flash_meizu_pattern_stop(struct cam_flash_meizu_pattern *pattern);
int cam_flash_meizu_sysfs_register(struct cam_flash_ctrl *ctrl);
void cam_flash_meizu_sysfs_unregister(struct cam_flash_ctrl *ctrl);
#endif
