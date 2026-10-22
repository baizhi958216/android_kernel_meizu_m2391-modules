/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _M2391_DTOF_H
#define _M2391_DTOF_H
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/types.h>

/* Exact userspace batch layout recovered from the original 0x8884410a ioctl.
 * The operation byte is zero for reads and nonzero for writes. Length counts
 * 16-bit big-endian wire words, with a maximum of 64 per operation.
 */
struct dtof_rw_reg_s {
	u8 write;
	u8 address;
	u16 length;
	u32 reserved;
	u16 words[64];
};
struct dtof_rw_reg_batch_s {
	u8 count;
	u8 reserved[3];
	struct dtof_rw_reg_s ops[16];
};
struct dtof_chip {
	struct i2c_client *client;
	struct mutex lock;
	struct kref ref;
	struct miscdevice misc;
	u32 reset_count;
	bool reset_state;
	int care_gpio;
	bool link_created;
	void *ipclog;
	int reset_gpio;
	bool removing;
};

int dtof_i2c_write_word(struct dtof_chip *, u8, u16);
int dtof_i2c_write_word_unlock(struct dtof_chip *, u8, u16);
int dtof_i2c_read_word(struct dtof_chip *, u8, u16 *);
int dtof_i2c_read_word_unlock(struct dtof_chip *, u8, u16 *);
int dtof_i2c_write_block(struct dtof_chip *, u8, const u8 *, u8);
int dtof_i2c_read_block(struct dtof_chip *, u8, u8 *, u8);
int dtof_i2c_read_ram_burst(struct dtof_chip *, u16 *, u16, u16);
int dtof_i2c_rw_batch(struct dtof_chip *, struct dtof_rw_reg_batch_s *);
int dtof_i2c_lock(struct dtof_chip *);
int dtof_i2c_unlock(struct dtof_chip *);
#endif
