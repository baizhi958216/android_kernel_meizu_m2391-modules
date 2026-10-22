/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_CS_F71_H
#define _MEIZU_CS_F71_H

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/kref.h>
#include <linux/proc_fs.h>

enum cs_f71_proc_id {
	CS_F71_PROC_INFO, CS_F71_PROC_BOOT, CS_F71_PROC_FW_FILE,
	CS_F71_PROC_REG, CS_F71_PROC_TEST, CS_F71_PROC_RAW,
	CS_F71_PROC_COEFFICIENT, CS_F71_PROC_FORCE, CS_F71_PROC_EFFECTIVE,
	CS_F71_PROC_FW_FORCE, CS_F71_PROC_LEFT, CS_F71_PROC_RIGHT,
	CS_F71_PROC_GAME, CS_F71_PROC_SLEEP, CS_F71_PROC_CAL_DATA,
	CS_F71_PROC_COUNT,
};

struct cs_f71;
struct cs_f71_proc {
	struct cs_f71 *cs;
	enum cs_f71_proc_id id;
};

/* Private source state; the original F71 module exports no symbols. */
struct cs_f71 {
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex command_lock;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *irq_gpio[2];
	struct input_dev *input[2];
	bool pressed[2];
	struct gpio_desc *enable_gpio;
	struct regulator *supply;
	bool supply_enabled;
	struct mutex irq_lock;
	struct work_struct event_work;
	int irq[2];
	bool irq_requested[2];
	bool irq_enabled;
	unsigned long irq_blocks;
	bool removed;
	struct miscdevice misc;
	struct kref ref;
	struct mutex operation_lock;
	struct mutex firmware_lock;
	struct work_struct firmware_work;
	bool firmware_pending;
	bool firmware_force;
	bool updating;
	struct proc_dir_entry *proc_root;
	struct cs_f71_proc proc_entries[CS_F71_PROC_COUNT];
	u8 register_cache[64];
	size_t register_cache_length;
	struct delayed_work boot_work;
	struct delayed_work panel_register_work;
	struct work_struct panel_work;
	int panel_desired; /* 0 unknown, 1 unblank, 2 blank/low power */
	struct workqueue_struct *panel_queue;
	void *panel_cookie;
	bool misc_registered;
	bool stopped;
};

int cs_f71_read(struct cs_f71 *cs, u8 reg, void *data, size_t length);
int cs_f71_write(struct cs_f71 *cs, u8 reg, const void *data, size_t length);
int cs_f71_write_flash(struct cs_f71 *cs, u16 reg,
		const void *data, size_t length);
int cs_f71_read_flash(struct cs_f71 *cs, u16 reg, void *data, size_t length);
/* Caller holds command_lock and has drained both interrupt producers. */
int cs_f71_update_firmware_locked(struct cs_f71 *cs, const u8 *image,
		size_t size, bool force);
int cs_f71_reset_locked(struct cs_f71 *cs);
int cs_f71_boot_info_locked(struct cs_f71 *cs, u8 info[4]);
int cs_f71_firmware_info_locked(struct cs_f71 *cs, u16 info[3]);
int cs_f71_wakeup_locked(struct cs_f71 *cs);
int cs_f71_sleep_locked(struct cs_f71 *cs);
int cs_f71_read_raw_locked(struct cs_f71 *cs, s16 values[8]);
int cs_f71_read_processed_locked(struct cs_f71 *cs, s16 values[4]);
int cs_f71_rw_test_locked(struct cs_f71 *cs, u8 value);
int cs_f71_sample_start_locked(struct cs_f71 *cs, u8 mode);
int cs_f71_set_threshold_locked(struct cs_f71 *cs, u16 left, u16 right);
int cs_f71_set_debug_locked(struct cs_f71 *cs, u8 mode);
int cs_f71_read_gears_locked(struct cs_f71 *cs, u16 gears[4]);
int cs_f71_set_gear_locked(struct cs_f71 *cs, unsigned int side, u16 value);
int cs_f71_set_game_locked(struct cs_f71 *cs, bool enabled);
int cs_f71_debug_capture_locked(struct cs_f71 *cs, u8 mode,
		u8 *bytes, size_t capacity);
int cs_f71_read_factor_locked(struct cs_f71 *cs, u8 channel, u16 *value);
int cs_f71_write_factor_locked(struct cs_f71 *cs, u8 channel, u16 value);
int cs_f71_read_level_locked(struct cs_f71 *cs, u16 *value);
int cs_f71_write_level_locked(struct cs_f71 *cs, u16 value);
int cs_f71_calibration_enable_locked(struct cs_f71 *cs, u8 channel,
		bool enable);
int cs_f71_input_init(struct cs_f71 *cs);
void cs_f71_input_exit(struct cs_f71 *cs);
void cs_f71_release_keys_locked(struct cs_f71 *cs);
void cs_f71_report_keys_locked(struct cs_f71 *cs);
int cs_f71_power_init(struct cs_f71 *cs);
void cs_f71_power_exit(struct cs_f71 *cs);
#define CS_F71_IRQ_EVENT BIT(0)
#define CS_F71_IRQ_FIRMWARE BIT(1)
#define CS_F71_IRQ_PANEL BIT(2)
#define CS_F71_IRQ_REMOVE BIT(3)
#define CS_F71_IRQ_SETUP BIT(4)
int cs_f71_irq_init(struct cs_f71 *cs);
void cs_f71_irq_exit(struct cs_f71 *cs);
void cs_f71_irq_block(struct cs_f71 *cs, unsigned long reason);
void cs_f71_irq_unblock(struct cs_f71 *cs, unsigned long reason);
int cs_f71_misc_init(struct cs_f71 *cs);
void cs_f71_misc_exit(struct cs_f71 *cs);
void cs_f71_put(struct cs_f71 *cs);
void cs_f71_firmware_work_init(struct cs_f71 *cs);
int cs_f71_queue_firmware(struct cs_f71 *cs, bool force);
void cs_f71_firmware_work_exit(struct cs_f71 *cs);
int cs_f71_proc_init(struct cs_f71 *cs);
void cs_f71_proc_exit(struct cs_f71 *cs);
ssize_t cs_f71_proc_write(struct file *file, const char __user *user,
		size_t count, loff_t *offset);
int cs_f71_runtime_init(struct cs_f71 *cs);
void cs_f71_runtime_start(struct cs_f71 *cs);
void cs_f71_runtime_exit(struct cs_f71 *cs);
/* Caller holds operation_lock, without command_lock. */
void cs_f71_apply_panel_state(struct cs_f71 *cs);

#endif
