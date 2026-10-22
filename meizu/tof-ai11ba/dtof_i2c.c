// SPDX-License-Identifier: GPL-2.0-only
/* M2391 AI11BA transport independently recovered from original ai11ba.ko.
 * See m2391-recovery/stock-i2c.disasm. No VL53 register protocol is used here.
 */
#include <linux/build_bug.h>
#include <linux/interrupt.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include "dtof.h"

static int dtof_transfer(struct dtof_chip *chip, struct i2c_msg *msg,
		int count, bool bus_locked)
{
	int rc = bus_locked ? __i2c_transfer(chip->client->adapter, msg, count) :
		i2c_transfer(chip->client->adapter, msg, count);

	if (rc == count)
		return 0;
	if (chip->ipclog)
		ipc_log_string(chip->ipclog,
			"%s i2c_transfer failed: %d msg cnt: %u", __func__, rc, count);
	dev_err(&chip->client->dev, "i2c_transfer failed: %d msg cnt: %u\n",
		rc, count);
	/* A partial transfer must not publish uninitialized sensor data. */
	return rc < 0 ? rc : -EIO;
}

static int dtof_write_word(struct dtof_chip *chip, u8 reg, u16 value,
		bool bus_locked)
{
	u8 data[3] = { reg, value >> 8, value };
	struct i2c_msg msg = {
		.addr = chip->client->addr, .len = sizeof(data), .buf = data,
	};

	return dtof_transfer(chip, &msg, 1, bus_locked);
}

int dtof_i2c_write_word(struct dtof_chip *chip, u8 reg, u16 value)
{
	return dtof_write_word(chip, reg, value, false);
}

int dtof_i2c_write_word_unlock(struct dtof_chip *chip, u8 reg, u16 value)
{
	return dtof_write_word(chip, reg, value, true);
}

static int dtof_read_word(struct dtof_chip *chip, u8 reg, u16 *value,
		bool bus_locked)
{
	u8 data[2];
	struct i2c_msg msg[2] = {
		{ .addr = chip->client->addr, .len = 1, .buf = &reg },
		{ .addr = chip->client->addr, .flags = I2C_M_RD,
		  .len = sizeof(data), .buf = data },
	};
	int rc = dtof_transfer(chip, msg, 2, bus_locked);

	if (!rc)
		*value = get_unaligned_be16(data);
	return rc;
}

int dtof_i2c_read_word(struct dtof_chip *chip, u8 reg, u16 *value)
{
	return dtof_read_word(chip, reg, value, false);
}

int dtof_i2c_read_word_unlock(struct dtof_chip *chip, u8 reg, u16 *value)
{
	return dtof_read_word(chip, reg, value, true);
}

int dtof_i2c_write_block(struct dtof_chip *chip, u8 reg, const u8 *data, u8 len)
{
	u8 *buffer = kmalloc((size_t)len + 1, GFP_KERNEL);
	struct i2c_msg msg = {
		.addr = chip->client->addr, .len = (u16)len + 1,
	};
	int rc;

	if (!buffer)
		return -ENOMEM;
	buffer[0] = reg;
	memcpy(buffer + 1, data, len);
	msg.buf = buffer;
	rc = dtof_transfer(chip, &msg, 1, false);
	kfree(buffer);
	return rc;
}

int dtof_i2c_read_block(struct dtof_chip *chip, u8 reg, u8 *data, u8 len)
{
	struct i2c_msg msg[2] = {
		{ .addr = chip->client->addr, .len = 1, .buf = &reg },
		{ .addr = chip->client->addr, .flags = I2C_M_RD,
		  .len = len, .buf = data },
	};

	return dtof_transfer(chip, msg, 2, false);
}

int dtof_i2c_read_ram_burst(struct dtof_chip *chip, u16 *words,
		u16 offset, u16 count)
{
	u8 address[3] = { 0xfe, offset >> 8, offset };
	u8 read = 0xff;
	struct i2c_msg msg[3] = {
		{ .addr = chip->client->addr, .len = sizeof(address), .buf = address },
		{ .addr = chip->client->addr, .len = 1, .buf = &read },
		{ .addr = chip->client->addr, .flags = I2C_M_RD,
		  .len = count * 2, .buf = (u8 *)words },
	};
	int rc, i;

	if (count > U16_MAX / 2)
		return -EINVAL;
	rc = dtof_transfer(chip, msg, 3, false);
	if (!rc)
		for (i = 0; i < count; ++i)
			words[i] = get_unaligned_be16((u8 *)words + 2 * i);
	return rc;
}

int dtof_i2c_rw_batch(struct dtof_chip *chip, struct dtof_rw_reg_batch_s *batch)
{
	struct dtof_batch_io {
		struct i2c_msg msg[32];
		u8 buffer[16][129];
	} *io;
	struct i2c_msg *msg;
	unsigned int i, j, n = 0;
	int rc;

	static_assert(sizeof(struct dtof_rw_reg_s) == 136);
	static_assert(sizeof(struct dtof_rw_reg_batch_s) == 2180);
	static_assert(offsetof(struct dtof_rw_reg_s, words) == 8);
	if (batch->count > ARRAY_SIZE(batch->ops))
		return -EINVAL;
	for (i = 0; i < batch->count; ++i)
		if (!batch->ops[i].write && batch->ops[i].length > 64)
			return -EINVAL;
	io = kzalloc(sizeof(*io), GFP_KERNEL);
	if (!io)
		return -ENOMEM;
	msg = io->msg;
	for (i = 0; i < batch->count; ++i) {
		struct dtof_rw_reg_s *op = &batch->ops[i];
		u16 length = min_t(u16, op->length, ARRAY_SIZE(op->words));

		if (op->write) {
			io->buffer[i][0] = op->address;
			for (j = 0; j < length; ++j)
				put_unaligned_be16(op->words[j], io->buffer[i] + 1 + 2 * j);
			msg[n++] = (struct i2c_msg) {
				.addr = chip->client->addr, .len = 1 + 2 * length,
				.buf = io->buffer[i],
			};
		} else {
			msg[n++] = (struct i2c_msg) {
				.addr = chip->client->addr, .len = 1, .buf = &op->address,
			};
			msg[n++] = (struct i2c_msg) {
				.addr = chip->client->addr, .flags = I2C_M_RD,
				.len = 2 * length, .buf = (u8 *)op->words,
			};
		}
	}
	rc = dtof_transfer(chip, msg, n, false);
	if (!rc)
		for (i = 0; i < batch->count; ++i)
			if (!batch->ops[i].write)
				for (j = 0; j < min_t(u16, batch->ops[i].length, 64); ++j)
					batch->ops[i].words[j] = get_unaligned_be16(
						(u8 *)batch->ops[i].words + 2 * j);
	kfree(io);
	return rc;
}

int dtof_i2c_lock(struct dtof_chip *chip)
{
	if (system_state > SYSTEM_RUNNING && irqs_disabled())
		return i2c_trylock_bus(chip->client->adapter, I2C_LOCK_SEGMENT) ?
			0 : -EAGAIN;
	i2c_lock_bus(chip->client->adapter, I2C_LOCK_SEGMENT);
	return 0;
}

int dtof_i2c_unlock(struct dtof_chip *chip)
{
	i2c_unlock_bus(chip->client->adapter, I2C_LOCK_SEGMENT);
	return 0;
}
