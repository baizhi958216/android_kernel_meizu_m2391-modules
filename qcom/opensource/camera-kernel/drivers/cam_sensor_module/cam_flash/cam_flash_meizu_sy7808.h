/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CAM_FLASH_MEIZU_SY7808_H
#define CAM_FLASH_MEIZU_SY7808_H
#include "cam_sensor_io.h"
#include <linux/mutex.h>
/* Source-owned equivalent of the stock provider's per-device state. */
struct meizu_sy7808 {
	struct camera_io_master *io;
	struct mutex lock;
	u32 enabled_channels;
	u32 mode;
	u32 users;
};
int meizu_sy7808_init(struct meizu_sy7808 *chip);
int meizu_sy7808_release(struct meizu_sy7808 *chip);
int meizu_sy7808_shutdown(struct meizu_sy7808 *chip);
int meizu_sy7808_enable(struct meizu_sy7808 *chip, u32 mode, u32 channels);
int meizu_sy7808_disable(struct meizu_sy7808 *chip, u32 channels);
int meizu_sy7808_set_duty(struct meizu_sy7808 *chip, u32 mode, u32 duty);
int meizu_sy7808_factory(struct meizu_sy7808 *chip, u8 selector,
	u8 command, u8 value, s32 brightness, u8 extra);
#endif
