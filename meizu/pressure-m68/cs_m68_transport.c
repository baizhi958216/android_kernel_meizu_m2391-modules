// SPDX-License-Identifier: GPL-2.0-only
/* Original M2391 M68 I2C framing, retries and firmware-update quiet period,
 * recovered from _cs_ReadReg (.text+0x470) and _cs_WriteReg (+0x890).
 */
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

#include "cs_m68.h"

static s64 cs_m68_subsecond_us(void)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	return now.tv_nsec / NSEC_PER_USEC;
}

/* io_lock serializes the quiet-period timestamp together with the transfer. */
static int cs_m68_transfer(struct cs_m68 *cs, struct i2c_msg *messages,
		unsigned int number, bool write)
{
	int attempt, ret = -EIO;

	mutex_lock(&cs->io_lock);
	if (READ_ONCE(cs->firmware_updating) && cs->last_write_us &&
	    cs_m68_subsecond_us() - cs->last_write_us < 10000)
		msleep(15);
	for (attempt = 0; attempt < 3; attempt++) {
		ret = i2c_transfer(cs->client->adapter, messages, number);
		if (ret == number)
			break;
		/* A short transfer is an error too, rather than usable data. */
		if (ret >= 0)
			ret = -EIO;
		msleep(10);
	}
	cs->last_write_us = write ? cs_m68_subsecond_us() : 0;
	mutex_unlock(&cs->io_lock);
	return ret;
}

int cs_m68_read(struct cs_m68 *cs, u8 reg, void *data, size_t length)
{
	struct i2c_msg messages[2] = {
		{ .addr = cs->client->addr, .len = 1, .buf = &reg },
		{ .addr = cs->client->addr, .flags = I2C_M_RD,
		  .len = length, .buf = data },
	};

	if (!data || !length || length > U16_MAX)
		return -EINVAL;
	return cs_m68_transfer(cs, messages, 2, false);
}

int cs_m68_write(struct cs_m68 *cs, u8 reg, const void *data, size_t length)
{
	struct i2c_msg message = { .addr = cs->client->addr };
	u8 *buffer;
	int ret;

	if (!data || !length || length >= U16_MAX)
		return -EINVAL;
	buffer = kmalloc(length + 1, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	buffer[0] = reg;
	memcpy(buffer + 1, data, length);
	message.buf = buffer;
	message.len = length + 1;
	ret = cs_m68_transfer(cs, &message, 1, true);
	kfree(buffer);
	return ret;
}

void cs_m68_wake(struct cs_m68 *cs)
{
	u8 status;

	cs_m68_read(cs, 0x02, &status, 1);
}
