// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of cs_press_f71.ko .text+0x4c8/0x748/0x1494. */
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

static int cs_f71_transfer(struct cs_f71 *cs, struct i2c_msg *messages,
		int count, unsigned int attempts)
{
	unsigned int attempt;
	int ret = -EIO;

	mutex_lock(&cs->io_lock);
	for (attempt = 0; attempt < attempts; attempt++) {
		ret = i2c_transfer(cs->client->adapter, messages, count);
		if (ret == count) {
			ret = 0;
			break;
		}
		/* Stock accepts short transfers; source never exposes partial data
		 * as a successful complete mailbox response.
		 */
		if (ret >= 0)
			ret = -EIO;
	}
	mutex_unlock(&cs->io_lock);
	return ret;
}

int cs_f71_read(struct cs_f71 *cs, u8 reg, void *data, size_t length)
{
	struct i2c_msg messages[2] = {
		{ .addr = cs->client->addr, .len = 1, .buf = &reg },
		{ .addr = cs->client->addr, .flags = I2C_M_RD,
		  .len = length, .buf = data },
	};

	if (!data || !length || length > U16_MAX)
		return -EINVAL;
	/* The original expands five immediate attempts with no delay. */
	return cs_f71_transfer(cs, messages, ARRAY_SIZE(messages), 5);
}

int cs_f71_write(struct cs_f71 *cs, u8 reg, const void *data, size_t length)
{
	struct i2c_msg message = { .addr = cs->client->addr, .len = length + 1 };
	u8 *packet;
	int ret;

	if (!data || !length || length > U8_MAX)
		return -EINVAL;
	packet = kmalloc(length + 1, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;
	packet[0] = reg;
	memcpy(packet + 1, data, length);
	message.buf = packet;
	ret = cs_f71_transfer(cs, &message, 1, 5);
	kfree(packet);
	return ret;
}

int cs_f71_write_flash(struct cs_f71 *cs, u16 reg,
		const void *data, size_t length)
{
	struct i2c_msg message = { .addr = cs->client->addr, .len = length + 2 };
	u8 *packet;
	int ret;

	if (!data || !length || length > U8_MAX)
		return -EINVAL;
	packet = kmalloc(length + 2, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;
	put_unaligned_be16(reg, packet);
	memcpy(packet + 2, data, length);
	message.buf = packet;
	/* Firmware's double-register transfer is a single attempt in stock. */
	ret = cs_f71_transfer(cs, &message, 1, 1);
	kfree(packet);
	return ret;
}

int cs_f71_read_flash(struct cs_f71 *cs, u16 reg, void *data, size_t length)
{
	u8 address[2];
	struct i2c_msg messages[2] = {
		{ .addr = cs->client->addr, .len = 2, .buf = address },
		{ .addr = cs->client->addr, .flags = I2C_M_RD,
		  .len = length, .buf = data },
	};

	if (!data || !length || length > U16_MAX)
		return -EINVAL;
	put_unaligned_be16(reg, address);
	return cs_f71_transfer(cs, messages, ARRAY_SIZE(messages), 1);
}
