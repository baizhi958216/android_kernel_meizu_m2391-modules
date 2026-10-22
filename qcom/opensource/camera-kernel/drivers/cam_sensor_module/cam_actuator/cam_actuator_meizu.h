/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CAM_ACTUATOR_MEIZU_H
#define CAM_ACTUATOR_MEIZU_H
#include "cam_actuator_dev.h"
int cam_actuator_meizu_sysfs_init(struct cam_actuator_ctrl_t *ctrl);
void cam_actuator_meizu_sysfs_remove(struct cam_actuator_ctrl_t *ctrl);
#endif
