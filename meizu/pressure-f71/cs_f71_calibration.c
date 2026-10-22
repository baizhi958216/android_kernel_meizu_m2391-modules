// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of stock .text 0x2598..0x2ce3. */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/lockdep.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

static int cs_f71_parameter_select(struct cs_f71 *cs, u8 mode)
{
	u8 zero = 0;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf6, &zero, 1);
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf7, &zero, 1);
	if (ret)
		return ret;
	return cs_f71_write(cs, 0xf6, &mode, 1);
}

static int cs_f71_parameter_read(struct cs_f71 *cs, u8 mode,
		u8 channel, u16 *value)
{
	u8 request[] = { channel, 0 }, response[4], length = 2;
	int ret;

	if (!value)
		return -EINVAL;
	ret = cs_f71_parameter_select(cs, mode);
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf8, request, sizeof(request));
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf7, &length, 1);
	if (ret)
		return ret;
	msleep(20);
	ret = cs_f71_read(cs, 0xf7, &length, 1);
	if (ret)
		return ret;
	if (length != sizeof(response))
		return -EPROTO;
	ret = cs_f71_read(cs, 0xf8, response, sizeof(response));
	if (!ret)
		*value = get_unaligned_le16(response);
	return ret;
}

int cs_f71_read_factor_locked(struct cs_f71 *cs, u8 channel, u16 *value)
{
	return cs_f71_parameter_read(cs, 0x31, channel, value);
}

int cs_f71_read_level_locked(struct cs_f71 *cs, u16 *value)
{
	return cs_f71_parameter_read(cs, 0x33, 0, value);
}

static int cs_f71_parameter_write(struct cs_f71 *cs, u8 mode,
		u8 channel, u16 value)
{
	u8 request[] = { channel, 0, 0, 0, 0, 0 }, length = sizeof(request);
	u16 observed;
	int ret;

	ret = cs_f71_parameter_select(cs, mode);
	if (ret)
		return ret;
	put_unaligned_le16(value, request + 2);
	ret = cs_f71_write(cs, 0xf8, request, sizeof(request));
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf7, &length, 1);
	if (ret)
		return ret;
	msleep(60);
	if (mode == 0x30)
		ret = cs_f71_read_factor_locked(cs, channel, &observed);
	else
		ret = cs_f71_read_level_locked(cs, &observed);
	if (ret)
		return ret;
	return observed == value ? 0 : -EIO;
}

int cs_f71_write_factor_locked(struct cs_f71 *cs, u8 channel, u16 value)
{
	return cs_f71_parameter_write(cs, 0x30, channel, value);
}

int cs_f71_write_level_locked(struct cs_f71 *cs, u16 value)
{
	return cs_f71_parameter_write(cs, 0x32, 0, value);
}

int cs_f71_calibration_enable_locked(struct cs_f71 *cs, u8 channel,
		bool enable)
{
	u8 command = enable ? channel + 1 : 0;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (enable && channel == U8_MAX)
		return -EINVAL;
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	msleep(1);
	return cs_f71_write(cs, 0x1c, &command, 1);
}
