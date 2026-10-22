/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _CAM_EEPROM_MEIZU_H_
#define _CAM_EEPROM_MEIZU_H_
#include <linux/types.h>
#include "cam_eeprom_dev.h"
struct meizu_camera_name { u32 id; const char *name; };
struct meizu_otp_range { u32 flag, start, end, high, low; };
extern const struct meizu_camera_name meizu_camera_module_list[18];
extern const struct meizu_camera_name meizu_camera_sensor_list[32];
extern const struct meizu_camera_name meizu_camera_actuator_list[5];
extern const struct meizu_otp_range mz_rear_e2p_sum[6];
extern const struct meizu_otp_range mz_face_e2p_sum[3];
extern const struct meizu_otp_range mz_admi_e2p_sum[14];
extern const struct meizu_otp_range mz_admi2_e2p_sum[14];
extern const struct meizu_otp_range mz_tof_e2p_sum[11];
int cam_eeprom_meizu_read_map(struct cam_eeprom_ctrl_t *ctrl,
		const struct cam_eeprom_memory_map_t *map, u8 *destination);
int cam_eeprom_meizu_apply_golden(u8 *bytes, u32 length, u32 index);
int cam_eeprom_meizu_sysfs_init(struct cam_eeprom_ctrl_t *ctrl);
void cam_eeprom_meizu_sysfs_remove(struct cam_eeprom_ctrl_t *ctrl);
void cam_eeprom_meizu_cache_otp(struct cam_eeprom_ctrl_t *ctrl);
int cam_eeprom_meizu_write_partition(struct cam_eeprom_ctrl_t *ctrl, u64 handle);
int meizu_bsp_partition_write(const u8 *bytes, u32 count, u32 offset, u8 index);
int cam_eeprom_meizu_write_bytes(struct cam_eeprom_ctrl_t *ctrl,
		const u8 *bytes, u32 count, u32 address);
#endif
