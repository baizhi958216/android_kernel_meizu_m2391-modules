/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_HAPTIC_RUNTIME_H
#define _MEIZU_HAPTIC_RUNTIME_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/wait.h>

/* Private state is owned by the source driver, with no exported layout ABI.
 * Field roles come from the original sysfs handlers and timeout worker.
 */
struct mz_haptic_runtime {
	int trig2_gpio;
	int play_time_ms;
	int stop_time_ms;
	int play_count;
	u32 restore_default_gain;
	u32 waveform;
	u32 cspress_level;
	u32 mback_level;
	u32 rtp_effect_id;
	u32 previous_waveform;
	u32 trig2_waveform;
	u32 selected_chip;
	bool timeout_active;
	u32 last_effect_id;
	int irq;
	int ram_update_result;
	char ram_filename[256];
	char rtp_filename[256];
	struct work_struct timeout_work;
	struct miscdevice misc;
	u8 file_command;
	u8 file_register;
	struct kref refcount;
	bool removed;
	bool misc_registered;
	struct mutex ioctl_lock;
	atomic_t firmware_pending;
	wait_queue_head_t firmware_wait;
	wait_queue_head_t timeout_wait;
	bool timeout_cancelled;
	bool hardware_ready;
	bool irq_registered;
	bool led_registered;
	bool led_group_registered;
	bool main_group_registered;
	bool backend_group_registered;
	bool motor_link_registered;
	bool class_link_registered;
	struct class *timed_output_class;
};

#endif
