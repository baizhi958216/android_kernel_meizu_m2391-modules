/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_CS_M68_H
#define _MEIZU_CS_M68_H

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>

struct input_dev;
struct task_struct;
struct regulator;

/* Private source state. No layout is exported by the original module. */
struct cs_m68 {
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex command_lock;
	struct mutex slot_lock;
	bool firmware_updating;
	s64 last_write_us;
	int enable_gpio;
	bool powered_down;
	int update_state;
	u16 target_version;
	struct input_dev *input;
	struct task_struct *event_thread;
	wait_queue_head_t event_wait;
	atomic_t event_pending;
	int irq_gpio;
	bool finger_down;
	struct mutex irq_lock;
	int irq;
	bool irq_live;
	bool irq_enabled;
	struct regulator *vdd;
	bool vdd_enabled;
	bool enable_gpio_owned;
	struct kref refcount;
	struct mutex access_lock;
	bool removed;
	struct miscdevice misc;
	u8 log_mask;
	struct mutex event_lock;
	struct workqueue_struct *workqueue;
	struct workqueue_struct *panel_workqueue;
	struct delayed_work update_work;
	struct delayed_work panel_register_work;
	struct delayed_work health_work;
	struct work_struct blank_work;
	struct work_struct unblank_work;
	void *panel_cookie;
	bool initialized;
	int shortcut_position;
	int shortcut_enabled;
	u32 calibration_value;
	u8 calibration_channel;
	struct kobject *root_kobject;
	bool sysfs_created;
	bool misc_registered;
	bool meizu_link_created;
	bool root_link_created;
	unsigned int debug_mode;
	int threshold_down;
	int threshold_up;
	int vibrate_down;
	int vibrate_up;
};

struct cs_m68_calibration {
	int valid;
	int channel0_low;
	int channel0_high;
	int channel1_low;
	int channel1_high;
	int update_failed;
};

struct cs_m68_firmware {
	const u8 *data;
	size_t length;
	u16 version;
};

int cs_m68_read(struct cs_m68 *cs, u8 reg, void *data, size_t length);
int cs_m68_write(struct cs_m68 *cs, u8 reg, const void *data, size_t length);
void cs_m68_wake(struct cs_m68 *cs);
int cs_m68_set_threshold(struct cs_m68 *cs, int down, int up, u8 channel);
int cs_m68_get_threshold(struct cs_m68 *cs, int *down, int *up, u8 channel);
int cs_m68_set_vibrate_time(struct cs_m68 *cs, int down, int up, u8 channel);
int cs_m68_get_vibrate_time(struct cs_m68 *cs, int *down, int *up);
int cs_m68_update_read_command(struct cs_m68 *cs);
int cs_m68_update_write_command(struct cs_m68 *cs);
int cs_m68_update_sign_command(struct cs_m68 *cs);
int cs_m68_update_reset_command(struct cs_m68 *cs);
int cs_m68_update_skip_command(struct cs_m68 *cs);
int cs_m68_check_bootloader(struct cs_m68 *cs);
int cs_m68_parse_firmware(const u8 *data, size_t size,
		struct cs_m68_firmware *firmware);
int cs_m68_power(struct cs_m68 *cs, bool enable);
int cs_m68_power_reset(struct cs_m68 *cs);
int cs_m68_check_application(struct cs_m68 *cs);
/* Caller holds command_lock when combining version reads with other commands. */
int cs_m68_read_version(struct cs_m68 *cs, u16 *version);
int cs_m68_burn_firmware(struct cs_m68 *cs,
		const struct cs_m68_firmware *firmware);
int cs_m68_update_firmware(struct cs_m68 *cs);
int cs_m68_set_calibration(struct cs_m68 *cs, u32 value, u8 channel);
int cs_m68_read_calibration(struct cs_m68 *cs, u32 *value);
int cs_m68_read_calibration_record(struct cs_m68 *cs,
		struct cs_m68_calibration *record);
int cs_m68_write_calibration_record(struct cs_m68 *cs,
		const struct cs_m68_calibration *record);
int cs_m68_set_update_failure(struct cs_m68 *cs, int failed);
int cs_m68_get_update_failure(struct cs_m68 *cs);
int cs_m68_restore_calibration(struct cs_m68 *cs);
int cs_m68_parse_hex_tokens(const char *buffer, size_t count, u32 *values,
		const u8 *digits, size_t capacity);
int cs_m68_read_pressure(struct cs_m68 *cs, u8 *state, s16 *pressure);
int cs_m68_start_events(struct cs_m68 *cs);
/* The caller must free/drain the IRQ before stopping the event thread. */
void cs_m68_stop_events(struct cs_m68 *cs);
irqreturn_t cs_m68_interrupt(int irq, void *data);
int cs_m68_set_irq(struct cs_m68 *cs, bool enabled);
int cs_m68_check_health(struct cs_m68 *cs);
int cs_m68_prepare_power(struct cs_m68 *cs);
void cs_m68_release_power(struct cs_m68 *cs);
void cs_m68_put(struct cs_m68 *cs);
extern const struct file_operations cs_m68_file_operations;
int cs_m68_load_log_mask(struct cs_m68 *cs);
int cs_m68_save_log_mask(struct cs_m68 *cs, u8 value);
int cs_m68_read_calibration_text(struct cs_m68 *cs, char *buffer, size_t size);
int cs_m68_write_calibration_text(struct cs_m68 *cs, const char *buffer, size_t size);
extern const struct attribute_group cs_m68_control_group;
extern const struct attribute_group cs_m68_pressure_group;
extern const struct attribute_group cs_m68_calibration_group;
extern const struct attribute_group cs_m68_diagnostic_group;
void cs_m68_release_key(struct cs_m68 *cs);
int cs_m68_start_runtime(struct cs_m68 *cs);
void cs_m68_stop_runtime(struct cs_m68 *cs);

#endif
