// SPDX-License-Identifier: GPL-2.0-only
/* M68 bootloader command primitives, original CsUpdata{R,W,WSign,Reset,Skip}
 * .text+0x22f0..0x27dc. The framing is register A1, then 55 A5 command.
 * Callers serialize the complete firmware operation with command_lock.
 */
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "cs_m68.h"

#include <linux/firmware.h>

int cs_m68_update_firmware(struct cs_m68 *cs)
{
	const struct firmware *file;
	struct cs_m68_firmware firmware;
	u16 current_version = 0;
	int ret;

	ret = request_firmware(&file, "cspress_m68fw.csf", &cs->client->dev);
	if (ret)
		return ret;
	ret = cs_m68_parse_firmware(file->data, file->size, &firmware);
	if (ret)
		goto out;
	msleep(250);
	ret = cs_m68_check_application(cs);
	if (!ret) {
		mutex_lock(&cs->command_lock);
		ret = cs_m68_read_version(cs, &current_version);
		mutex_unlock(&cs->command_lock);
		/* A failed version read follows stock's initialized zero fallback. */
		if (ret)
			current_version = 0;
		if (current_version >= firmware.version) {
			ret = 0;
			goto out;
		}
	}
	ret = cs_m68_burn_firmware(cs, &firmware);
out:
	release_firmware(file);
	return ret;
}

int cs_m68_check_bootloader(struct cs_m68 *cs)
{
	u8 identity;
	int ret, attempt, sample;

	ret = cs_m68_power_reset(cs);
	if (ret)
		return ret;
	msleep(40);
	for (attempt = 0; attempt < 4; attempt++) {
		ret = cs_m68_read(cs, 0x00, &identity, 1);
		if (ret < 0)
			continue;
		if ((identity & 0xf0) == 0x10)
			return 0;
		ret = cs_m68_power_reset(cs);
		if (ret)
			return ret;
		msleep(40);
		/* The original unrolls these ten immediate identity samples. */
		for (sample = 0; sample < 10; sample++) {
			ret = cs_m68_read(cs, 0x00, &identity, 1);
			if (ret >= 0 && (identity & 0xf0) == 0x10)
				return 0;
		}
	}
	return -ENODEV;
}

int cs_m68_parse_firmware(const u8 *data, size_t size,
		struct cs_m68_firmware *firmware)
{
	size_t length;

	/* Original CSF image: 18 68, big-endian version at 2, byte count at 6,
	 * and a 32-byte header before the page stream. Check all bounds before
	 * exposing it to the hardware updater (the original omitted these).
	 */
	if (!data || !firmware || size < 32 || data[0] != 0x18 || data[1] != 0x68)
		return -EINVAL;
	length = get_unaligned_be16(data + 6);
	if (!length || length % 32 || length > size - 32)
		return -EINVAL;
	firmware->version = get_unaligned_be16(data + 2);
	firmware->data = data + 32;
	firmware->length = length;
	return 0;
}

static int cs_m68_update_command(struct cs_m68 *cs, u8 command)
{
	u8 payload[] = { 0x55, 0xa5, command };
	int ret = cs_m68_write(cs, 0xa1, payload, sizeof(payload));

	return ret < 0 ? ret : 0;
}

int cs_m68_update_read_command(struct cs_m68 *cs)
{
	return cs_m68_update_command(cs, 0xa5);
}

int cs_m68_update_write_command(struct cs_m68 *cs)
{
	return cs_m68_update_command(cs, 0x5a);
}

int cs_m68_update_sign_command(struct cs_m68 *cs)
{
	return cs_m68_update_command(cs, 0x51);
}

int cs_m68_update_reset_command(struct cs_m68 *cs)
{
	u8 value = 0;
	int ret;

	ret = cs_m68_write(cs, 0x01, &value, 1);
	if (ret < 0)
		return ret;
	msleep(5);
	ret = cs_m68_read(cs, 0x00, &value, 1);
	if (ret < 0)
		return ret;
	msleep(5);
	return cs_m68_update_command(cs, 0x55);
}

int cs_m68_update_skip_command(struct cs_m68 *cs)
{
	return cs_m68_update_command(cs, 0xaa);
}

int cs_m68_read_version(struct cs_m68 *cs, u16 *version)
{
	u8 bytes[2] = { 0 };
	u16 previous = 0xfefe, observed;
	int attempt, ret;

	if (!version)
		return -EINVAL;
	/* Original readFW_Version samples up to six times, accepting a stable
	 * pair. A low byte of 4D is an invalid transient and is not remembered.
	 */
	for (attempt = 0; attempt < 6; attempt++) {
		ret = cs_m68_read(cs, 0x05, bytes, sizeof(bytes));
		if (ret < 0)
			continue;
		observed = get_unaligned_le16(bytes);
		if (bytes[0] != 0x4d) {
			if (observed == previous) {
				*version = observed;
				return 0;
			}
			previous = observed;
		}
		msleep(20);
	}
	return -EIO;
}

int cs_m68_burn_firmware(struct cs_m68 *cs,
		const struct cs_m68_firmware *firmware)
{
	u8 *readback, status;
	u16 version;
	size_t offset;
	int attempt, ret = -EIO;
	bool transferred;

	if (!firmware || !firmware->data || !firmware->length ||
	    firmware->length % 32 || firmware->length > U16_MAX)
		return -EINVAL;
	readback = kmalloc(firmware->length, GFP_KERNEL);
	if (!readback)
		return -ENOMEM;
	mutex_lock(&cs->command_lock);
	cs->target_version = firmware->version;
	WRITE_ONCE(cs->update_state, 2);
	WRITE_ONCE(cs->firmware_updating, true);
	for (attempt = 0; attempt < 3; attempt++) {
		ret = cs_m68_power_reset(cs);
		msleep(50);
		if (ret < 0)
			continue;
		ret = cs_m68_update_write_command(cs);
		if (ret < 0)
			continue;
		msleep(50);
		transferred = true;
		for (offset = 0; offset < firmware->length; offset += 32) {
			/* The bootloader consumes an eight-bit sequential page token,
			 * including its wrap: the shipped image contains 424 pages.
			 */
			ret = cs_m68_write(cs, (u8)(offset / 32),
					  firmware->data + offset, 32);
			msleep(20);
			if (ret < 0) {
				transferred = false;
				break;
			}
		}
		msleep(10);
		cs_m68_read(cs, 0x00, &status, 1);
		msleep(10);
		ret = cs_m68_update_read_command(cs);
		if (ret < 0)
			continue;
		msleep(1);
		memset(readback, 0, firmware->length);
		if (!transferred) {
			ret = -EIO;
			continue;
		}
		for (offset = 0; offset < firmware->length; offset += 32) {
			ret = cs_m68_read(cs, (u8)(offset / 32), readback + offset, 32);
			msleep(1);
			if (ret < 0) {
				transferred = false;
				break;
			}
		}
		if (!transferred || memcmp(firmware->data, readback, firmware->length)) {
			ret = -EIO;
			continue;
		}
		msleep(10);
		ret = cs_m68_update_sign_command(cs);
		msleep(50);
		if (ret < 0)
			continue;
		ret = cs_m68_power_reset(cs);
		msleep(300);
		if (ret < 0)
			continue;
		ret = cs_m68_read_version(cs, &version);
		if (!ret && version == firmware->version)
			break;
		ret = -EIO;
	}
	WRITE_ONCE(cs->firmware_updating, false);
	WRITE_ONCE(cs->update_state, ret ? 0 : 1);
	mutex_unlock(&cs->command_lock);
	kfree(readback);
	return ret;
}
