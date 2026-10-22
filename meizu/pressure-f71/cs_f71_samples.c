// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of stock raw/processed readers at 0x2048/0x22c0. */
#include <linux/errno.h>
#include <linux/lockdep.h>
#include <linux/delay.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

int cs_f71_sample_start_locked(struct cs_f71 *cs, u8 mode)
{
	u8 zero = 0;
	unsigned int attempt, attempts;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	/* Original raw, processed and NDT debug initializers respectively. */
	if (mode != 0x01 && mode != 0x14 && mode != 0xf8)
		return -EINVAL;
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf6, &zero, 1);
	if (ret)
		return ret;
	ret = cs_f71_write(cs, 0xf7, &zero, 1);
	if (ret)
		return ret;
	attempts = mode == 0xf8 ? 4 : 1;
	for (attempt = 0; attempt < attempts; attempt++) {
		ret = cs_f71_write(cs, 0xf6, &mode, 1);
		if (!ret)
			return 0;
	}
	return ret;
}

static int cs_f71_sample_finish(struct cs_f71 *cs, int result)
{
	u8 zero = 0;
	int ret = cs_f71_write(cs, 0xf7, &zero, 1);

	return result < 0 ? result : (ret ? ret : result);
}

int cs_f71_read_raw_locked(struct cs_f71 *cs, s16 values[8])
{
	u8 length, bytes[16];
	unsigned int i;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (!values)
		return -EINVAL;
	ret = cs_f71_read(cs, 0xf7, &length, 1);
	if (ret)
		goto out;
	if (!length || length > sizeof(bytes) || (length & 1)) {
		ret = -EPROTO;
		goto out;
	}
	ret = cs_f71_read(cs, 0xf8, bytes, length);
	if (ret)
		goto out;
	for (i = 0; i < length / 2; i++)
		values[i] = get_unaligned_le16(bytes + 2 * i);
	ret = length / 2;
out:
	return cs_f71_sample_finish(cs, ret);
}

int cs_f71_read_processed_locked(struct cs_f71 *cs, s16 values[4])
{
	u8 length, bytes[10];
	u16 sum = 0;
	unsigned int i;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (!values)
		return -EINVAL;
	ret = cs_f71_read(cs, 0xf7, &length, 1);
	if (ret)
		goto out;
	if (length != sizeof(bytes)) {
		ret = -EPROTO;
		goto out;
	}
	ret = cs_f71_read(cs, 0xf8, bytes, sizeof(bytes));
	if (ret)
		goto out;
	for (i = 0; i < 4; i++)
		sum += get_unaligned_le16(bytes + 2 * i);
	/* Stock returns success but leaves the caller's output unchanged on
	 * a corrupt checksum. Report it explicitly instead of stale samples.
	 */
	if (sum != get_unaligned_le16(bytes + 8)) {
		ret = -EBADMSG;
		goto out;
	}
	for (i = 0; i < 4; i++)
		values[i] = get_unaligned_le16(bytes + 2 * i);
out:
	return cs_f71_sample_finish(cs, ret);
}

int cs_f71_debug_capture_locked(struct cs_f71 *cs, u8 mode,
		u8 *bytes, size_t capacity)
{
	u8 length = 0;
	unsigned int attempt;
	int ret, cleanup;

	lockdep_assert_held(&cs->command_lock);
	/* The actual proc readers use 0x10 raw and 0x21 force; the older
	 * mode1/mode0x14 standalone sample routines are a different protocol.
	 */
	if (!bytes || !capacity || (mode != 0x10 && mode != 0x21))
		return -EINVAL;
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_set_debug_locked(cs, mode);
	if (ret)
		goto out;
	for (attempt = 0; attempt < 50; attempt++) {
		msleep(5);
		ret = cs_f71_read(cs, 0xf7, &length, 1);
		if (ret)
			goto out;
		if (length)
			break;
	}
	if (!length) {
		ret = -ETIMEDOUT;
		goto out;
	}
	/* Original proc readers trusted a device-controlled length when
	 * copying into a 64-byte stack array. Bound the response first.
	 */
	if (length > capacity) {
		ret = -EMSGSIZE;
		goto out;
	}
	ret = cs_f71_read(cs, 0xf8, bytes, length);
	if (!ret)
		ret = length;
out:
	cleanup = cs_f71_set_debug_locked(cs, 0);
	if (!cleanup)
		cleanup = cs_f71_sample_finish(cs, 0);
	return ret < 0 ? ret : (cleanup ? cleanup : ret);
}
