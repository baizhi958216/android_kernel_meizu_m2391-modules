// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of stock .text 0x1078..0x197b. */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/lockdep.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "cs_f71.h"

#define CS_F71_FW_HEADER 256
#define CS_F71_FW_PAGE 128

int cs_f71_reset_locked(struct cs_f71 *cs)
{
	lockdep_assert_held(&cs->command_lock);
	if (IS_ERR_OR_NULL(cs->reset_gpio))
		return -ENODEV;
	gpiod_set_raw_value_cansleep(cs->reset_gpio, 1);
	msleep(100);
	gpiod_set_raw_value_cansleep(cs->reset_gpio, 0);
	msleep(80);
	return 0;
}

int cs_f71_boot_info_locked(struct cs_f71 *cs, u8 info[4])
{
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (!info)
		return -EINVAL;
	ret = cs_f71_reset_locked(cs);
	if (ret)
		return ret;
	/* Original double-register prefix is f1 00, not 00 f1. */
	return cs_f71_read_flash(cs, 0xf100, info, 4);
}

int cs_f71_firmware_info_locked(struct cs_f71 *cs, u16 info[3])
{
	u8 bytes[6];
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (!info)
		return -EINVAL;
	ret = cs_f71_rw_test_locked(cs, 0x67);
	if (ret)
		return ret;
	ret = cs_f71_read(cs, 0x03, bytes, 2);
	if (ret)
		return ret;
	info[0] = get_unaligned_le16(bytes);
	ret = cs_f71_read(cs, 0x04, bytes, 2);
	if (ret)
		return ret;
	info[1] = get_unaligned_le16(bytes);
	ret = cs_f71_read(cs, 0x10, bytes, sizeof(bytes));
	if (!ret)
		info[2] = get_unaligned_be16(bytes + 2);
	return ret;
}

static int cs_f71_flash_once(struct cs_f71 *cs, const u8 *payload,
		size_t length, u16 version, u8 *readback)
{
	static const u8 start[] = { 0xaa, 0x55, 0xa5, 0x5a };
	static const u8 finish[] = { 0x50, 0x41, 0x53, 0x73 };
	u8 info[6];
	size_t offset;
	int ret;

	ret = cs_f71_reset_locked(cs);
	if (ret)
		return ret;
	ret = cs_f71_write_flash(cs, 0, start, sizeof(start));
	if (ret)
		return ret;
	msleep(1500);
	for (offset = 0; offset < length; offset += CS_F71_FW_PAGE) {
		ret = cs_f71_write_flash(cs, offset, payload + offset,
				CS_F71_FW_PAGE);
		if (ret)
			return ret;
		msleep(10);
	}
	/* Stock verifies complete 256-byte blocks only. Preserve that wire
	 * protocol; the shipped image has an additional final 128-byte page.
	 */
	for (offset = 0; offset + 256 <= length; offset += 256) {
		ret = cs_f71_read_flash(cs, offset, readback, 256);
		if (ret)
			return ret;
		if (memcmp(readback, payload + offset, 256))
			return -EIO;
		msleep(5);
	}
	ret = cs_f71_write_flash(cs, 0, finish, sizeof(finish));
	if (ret)
		return ret;
	msleep(50);
	ret = cs_f71_reset_locked(cs);
	if (ret)
		return ret;
	msleep(300);
	ret = cs_f71_read(cs, 0x10, info, sizeof(info));
	if (ret)
		return ret;
	return get_unaligned_be16(info + 2) == version ? 0 : -EIO;
}

int cs_f71_update_firmware_locked(struct cs_f71 *cs, const u8 *image,
		size_t size, bool force)
{
	u8 info[6], *readback;
	u16 version, length;
	unsigned int attempt;
	int ret;

	lockdep_assert_held(&cs->command_lock);
	if (!image || size < CS_F71_FW_HEADER)
		return -EINVAL;
	length = get_unaligned_be16(image + 14);
	/* Stock fml_firmware_send_data requires an exact header+payload size.
	 * Reject partial pages instead of silently truncating their writes.
	 */
	if (!length || length % CS_F71_FW_PAGE ||
	    size != CS_F71_FW_HEADER + (size_t)length)
		return -EINVAL;
	version = get_unaligned_be16(image + 10);
	if (!force) {
		msleep(300);
		ret = cs_f71_read(cs, 0x10, info, sizeof(info));
		/* Despite its original name, high_version_update compares equality,
		 * and also flashes a different lower version.
		 */
		if (!ret && get_unaligned_be16(info + 2) == version)
			return 0;
	}
	readback = kmalloc(256, GFP_KERNEL);
	if (!readback)
		return -ENOMEM;
	for (attempt = 0; attempt < (force ? 1 : 4); attempt++) {
		ret = cs_f71_flash_once(cs, image + CS_F71_FW_HEADER,
				length, version, readback);
		if (!ret)
			break;
	}
	kfree(readback);
	return ret;
}
