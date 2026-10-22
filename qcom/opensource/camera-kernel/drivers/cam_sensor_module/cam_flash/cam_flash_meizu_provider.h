/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CAM_FLASH_MEIZU_PROVIDER_H
#define CAM_FLASH_MEIZU_PROVIDER_H
#include <linux/types.h>
/* Seven pointer slots verified from stock sy7808_func_ops relocations.
 * Slot 1 is NULL in the original provider and has no observed call site.
 * Descriptive source names do not claim the unavailable original type names. */
struct meizu_flashlight_operations {
	int (*init)(uint32_t index);
	void *reserved;
	int (*enable)(uint32_t index, uint32_t mode, uint32_t channels);
	int (*disable)(uint32_t index, uint32_t channels);
	int (*duty)(uint32_t index, uint32_t mode, uint32_t duty);
	int (*release)(uint32_t index);
	int (*shutdown)(uint32_t index);
};
int flashlight_dev_register(uint32_t index, struct meizu_flashlight_operations *ops);
int flashlight_dev_unregister(uint32_t index);
int cam_meizu_flash_register(void);
void cam_meizu_flash_unregister(void);
int cam_meizu_flash_provider_init(unsigned int index);
int cam_meizu_flash_provider_release(unsigned int index);
int cam_meizu_flash_provider_shutdown(unsigned int index);
int cam_meizu_flash_provider_enable(unsigned int index, unsigned int mode,
	unsigned int channels);
int cam_meizu_flash_provider_disable(unsigned int index, unsigned int channels);
int cam_meizu_flash_provider_duty(unsigned int index, unsigned int mode,
	unsigned int duty);
int cam_meizu_flash_provider_factory(unsigned int index, unsigned int selector,
	unsigned int command, unsigned int value, int brightness, unsigned int extra);
#endif
