// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of stock .text 0x1c74..0x1f83. */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/lockdep.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

int cs_f71_rw_test_locked(struct cs_f71 *cs, u8 value)
{
	u8 response;
	unsigned int attempt;
	int ret = -EIO;

	lockdep_assert_held(&cs->command_lock);
	for (attempt = 0; attempt < 3; attempt++) {
		ret = cs_f71_write(cs, 0x00, &value, 1);
		if (!ret)
			ret = cs_f71_read(cs, 0x00, &response, 1);
		if (!ret && response == value)
			return 0;
		if (!ret)
			ret = -EIO;
		msleep(1);
	}
	return ret;
}

int cs_f71_wakeup_locked(struct cs_f71 *cs)
{
	u8 command = 1;
	unsigned int attempt;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	for (attempt = 0; attempt < 4; attempt++) {
		ret = cs_f71_write(cs, 0x06, &command, 1);
		if (!ret)
			return 0;
		if (attempt != 3)
			msleep(1);
	}
	return ret;
}

int cs_f71_sleep_locked(struct cs_f71 *cs)
{
	static const u8 command[] = { 0x00, 0x01, 0xff };
	u8 response[3];
	unsigned int attempt;
	int ret = -EIO;

	lockdep_assert_held(&cs->command_lock);
	for (attempt = 0; attempt < 5; attempt++) {
		ret = cs_f71_write(cs, 0x55, command, sizeof(command));
		if (!ret) {
			msleep(1);
			ret = cs_f71_read(cs, 0x55, response, sizeof(response));
			/* Stock tests full integer addition, not an 8-bit checksum:
			 * the accepted acknowledgement is three zero bytes.
			 */
			if (!ret && !response[0] && !response[1] && !response[2])
				return 0;
			if (!ret)
				ret = -EIO;
		}
		msleep(1);
	}
	return ret;
}

int cs_f71_set_threshold_locked(struct cs_f71 *cs, u16 left, u16 right)
{
	u8 packet[8] = { 0, 0, 0, 0, 3, 0, 0, 0 };
	unsigned int attempt, i;
	u8 checksum = 0;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	put_unaligned_le16(left, packet);
	put_unaligned_le16(right, packet + 2);
	for (i = 0; i < 5; i++)
		checksum -= packet[i];
	packet[5] = checksum;
	for (attempt = 0; attempt < 4; attempt++) {
		ret = cs_f71_write(cs, 0x5b, packet, sizeof(packet));
		if (!ret)
			return 0;
		if (attempt != 3)
			msleep(1);
	}
	return ret;
}

int cs_f71_set_debug_locked(struct cs_f71 *cs, u8 mode)
{
	u8 zero = 0;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	ret = cs_f71_write(cs, 0xf6, &mode, 1);
	if (ret)
		return ret;
	return cs_f71_write(cs, 0xf7, &zero, 1);
}

int cs_f71_read_gears_locked(struct cs_f71 *cs, u16 gears[4])
{
	u8 packet[8];
	unsigned int i;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (!gears)
		return -EINVAL;
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_read(cs, 0x5b, packet, sizeof(packet));
	if (!ret)
		for (i = 0; i < 4; i++)
			gears[i] = get_unaligned_le16(packet + i * 2);
	return ret;
}

int cs_f71_set_gear_locked(struct cs_f71 *cs, unsigned int side, u16 value)
{
	static const u8 commit[] = { 0x5b, 0xa5 };
	u8 packet[8];
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (side > 1)
		return -EINVAL;
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_read(cs, 0x5b, packet, sizeof(packet));
	if (ret)
		return ret;
	/* Actual left/right proc writers preserve the other side and use
	 * four LE16 thresholds, unlike the older standalone command above.
	 */
	put_unaligned_le16(value, packet + side * 4);
	put_unaligned_le16((unsigned int)value * 6 / 10, packet + side * 4 + 2);
	ret = cs_f71_write(cs, 0x5b, packet, sizeof(packet));
	if (ret)
		return ret;
	return cs_f71_write(cs, 0x01, commit, sizeof(commit));
}

int cs_f71_set_game_locked(struct cs_f71 *cs, bool enabled)
{
	static const u8 commit[] = { 0x55, 0xab };
	u8 mode = enabled ? 2 : 0;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	/* The sleep proc node invokes the same command with inverted input. */
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0x55, &mode, 1);
	if (ret)
		return ret;
	return cs_f71_write(cs, 0x01, commit, sizeof(commit));
}
