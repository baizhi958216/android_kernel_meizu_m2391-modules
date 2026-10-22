/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _CAM_OIS_MEIZU_H_
#define _CAM_OIS_MEIZU_H_
#include "cam_ois_dev.h"
int cam_ois_meizu_sysfs_init(struct cam_ois_ctrl_t *ctrl);
void cam_ois_meizu_sysfs_remove(struct cam_ois_ctrl_t *ctrl);
int mz_ois_self_test(void);
#endif
