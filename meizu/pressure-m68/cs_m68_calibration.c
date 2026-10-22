// SPDX-License-Identifier: GPL-2.0-only
/* M2391 M68: raw calibration mailbox FB/FC/FD and slot73 ASCII record,
 * recovered from original .text+0x1150..0x20a8. No F71 code is used here.
 */
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "../storage/meizu_storage.h"
#include "cs_m68.h"

#define CS_M68_SLOT 73
#define CS_M68_SLOT_BYTES 1024
#define CS_M68_RECORD_BYTES 256

static int cs_m68_slot_rw(bool write, void *buffer, size_t length)
{
	int attempt, ret = -EIO;

	if (!buffer || length > CS_M68_SLOT_BYTES)
		return -EINVAL;
	for (attempt = 0; attempt < 10; attempt++) {
		ret = private_slot_rw(write, CS_M68_SLOT, buffer, length);
		if (ret >= 0)
			break;
		msleep(1000);
	}
	return ret;
}

static bool cs_m68_binary_log_record(const u8 bytes[6])
{
	return !bytes[0] && !bytes[1] && !bytes[2] && !bytes[3] && !bytes[4];
}

int cs_m68_load_log_mask(struct cs_m68 *cs)
{
	u8 bytes[6];
	int ret;

	mutex_lock(&cs->slot_lock);
	ret = cs_m68_slot_rw(false, bytes, sizeof(bytes));
	if (ret >= 0) {
		/* The same slot also contains ASCII calibration. Its byte five
		 * must not be interpreted as the legacy binary logging record.
		 */
		cs->log_mask = cs_m68_binary_log_record(bytes) ? bytes[5] : 0;
		ret = 0;
	}
	mutex_unlock(&cs->slot_lock);
	return ret;
}

int cs_m68_save_log_mask(struct cs_m68 *cs, u8 value)
{
	u8 bytes[6];
	int ret;

	mutex_lock(&cs->slot_lock);
	cs->log_mask = value;
	ret = cs_m68_slot_rw(false, bytes, sizeof(bytes));
	if (ret >= 0 && cs_m68_binary_log_record(bytes)) {
		bytes[5] = value;
		ret = cs_m68_slot_rw(true, bytes, sizeof(bytes));
	}
	/* Original set_log_status passed NULL sscanf destinations and then
	 * overwrote six bytes of calibration text. Keep RAM logging usable
	 * without destroying an ASCII calibration record to persist it.
	 */
	mutex_unlock(&cs->slot_lock);
	return ret < 0 ? ret : 0;
}

int cs_m68_read_calibration_text(struct cs_m68 *cs, char *buffer, size_t size)
{
	int ret;

	if (!buffer || size < CS_M68_RECORD_BYTES + 1)
		return -EINVAL;
	mutex_lock(&cs->slot_lock);
	ret = cs_m68_slot_rw(false, buffer, CS_M68_RECORD_BYTES);
	buffer[CS_M68_RECORD_BYTES] = '\0';
	mutex_unlock(&cs->slot_lock);
	return ret < 0 ? ret : 0;
}

int cs_m68_write_calibration_text(struct cs_m68 *cs, const char *buffer, size_t size)
{
	struct cs_m68_calibration parsed;
	char *record;
	int ret;

	if (!buffer || !size || size > CS_M68_SLOT_BYTES)
		return -EINVAL;
	record = kzalloc(CS_M68_SLOT_BYTES + 1, GFP_KERNEL);
	if (!record)
		return -ENOMEM;
	memcpy(record, buffer, size);
	ret = sscanf(record, "%d %d %d %d %d", &parsed.valid,
		&parsed.channel0_low, &parsed.channel0_high,
		&parsed.channel1_low, &parsed.channel1_high);
	if (ret != 5) {
		ret = -EINVAL;
		goto out;
	}
	mutex_lock(&cs->slot_lock);
	ret = cs_m68_slot_rw(true, record, CS_M68_SLOT_BYTES);
	mutex_unlock(&cs->slot_lock);
out:
	kfree(record);
	return ret < 0 ? ret : 0;
}

int cs_m68_read_calibration(struct cs_m68 *cs, u32 *value)
{
	u8 zero = 0, mode = 0x31, length = 0, data[6] = { 0 };
	u16 sum;
	int ret;

	if (!value)
		return -EINVAL;
	mutex_lock(&cs->command_lock);
	ret = cs_m68_write(cs, 0xfb, &zero, 1);
	if (ret >= 0)
		ret = cs_m68_write(cs, 0xfc, &zero, 1);
	if (ret >= 0)
		ret = cs_m68_write(cs, 0xfb, &mode, 1);
	if (ret >= 0) {
		msleep(20);
		ret = cs_m68_read(cs, 0xfc, &length, 1);
	}
	if (ret >= 0 && length == sizeof(data)) {
		ret = cs_m68_read(cs, 0xfd, data, sizeof(data));
		if (ret >= 0) {
			sum = -(get_unaligned_le16(data) + get_unaligned_le16(data + 2));
			if (sum != get_unaligned_le16(data + 4))
				ret = -EBADMSG;
			else {
				*value = get_unaligned_le32(data);
				ret = 0;
			}
		}
	} else if (ret >= 0) {
		ret = -EBADMSG;
	}
	cs_m68_write(cs, 0xfb, &zero, 1);
	mutex_unlock(&cs->command_lock);
	return ret;
}

/* Caller serializes read/modify/write across the whole slot protocol. */
static int cs_m68_read_record(struct cs_m68_calibration *record)
{
	char buffer[CS_M68_RECORD_BYTES + 1] = { 0 };
	struct cs_m68_calibration parsed = { 0 };
	size_t i;
	int ret = cs_m68_slot_rw(false, buffer, CS_M68_RECORD_BYTES);

	if (ret < 0)
		return ret;
	for (i = 0; i < CS_M68_RECORD_BYTES; i++)
		if (buffer[i] && !isspace(buffer[i]))
			break;
	if (i != CS_M68_RECORD_BYTES) {
		ret = sscanf(buffer, "%d %d %d %d %d %d", &parsed.valid,
			&parsed.channel0_low, &parsed.channel0_high,
			&parsed.channel1_low, &parsed.channel1_high, &parsed.update_failed);
		/* Factory calibration is five fields; the failure flag is appended
		 * later by write_updateFailFlag. Both are real original formats.
		 */
		if (ret < 5)
			return -EINVAL;
	}
	*record = parsed;
	return 0;
}

static int cs_m68_write_record(const struct cs_m68_calibration *record)
{
	char *buffer;
	int ret;

	buffer = kzalloc(CS_M68_SLOT_BYTES, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	ret = snprintf(buffer, CS_M68_RECORD_BYTES, "%d %d %d %d %d %d",
		record->valid, record->channel0_low, record->channel0_high,
		record->channel1_low, record->channel1_high, record->update_failed);
	if (ret >= 0 && ret < CS_M68_RECORD_BYTES)
		/* Original clears 1024 then writes 256. One write has that same final
		 * record, without an intermediate completely erased calibration.
		 */
		ret = cs_m68_slot_rw(true, buffer, CS_M68_SLOT_BYTES);
	else
		ret = -EOVERFLOW;
	kfree(buffer);
	return ret < 0 ? ret : 0;
}

int cs_m68_read_calibration_record(struct cs_m68 *cs,
		struct cs_m68_calibration *record)
{
	int ret;

	if (!record)
		return -EINVAL;
	mutex_lock(&cs->slot_lock);
	ret = cs_m68_read_record(record);
	mutex_unlock(&cs->slot_lock);
	return ret;
}

int cs_m68_write_calibration_record(struct cs_m68 *cs,
		const struct cs_m68_calibration *record)
{
	int ret;

	if (!record)
		return -EINVAL;
	mutex_lock(&cs->slot_lock);
	ret = cs_m68_write_record(record);
	mutex_unlock(&cs->slot_lock);
	return ret;
}

int cs_m68_set_update_failure(struct cs_m68 *cs, int failed)
{
	struct cs_m68_calibration record;
	int ret;

	mutex_lock(&cs->slot_lock);
	ret = cs_m68_read_record(&record);
	if (!ret) {
		record.update_failed = failed;
		ret = cs_m68_write_record(&record);
	}
	mutex_unlock(&cs->slot_lock);
	return ret;
}

int cs_m68_get_update_failure(struct cs_m68 *cs)
{
	struct cs_m68_calibration record;
	int ret = cs_m68_read_calibration_record(cs, &record);

	return ret < 0 ? ret : record.update_failed;
}

int cs_m68_restore_calibration(struct cs_m68 *cs)
{
	struct cs_m68_calibration record;
	u32 desired, observed;
	int attempt, ret = cs_m68_read_calibration_record(cs, &record);

	if (ret < 0)
		return ret;
	if (record.valid != 1)
		return 0;
	desired = (u32)record.channel0_low + ((u32)record.channel0_high << 16);
	for (attempt = 0; attempt < 5; attempt++) {
		ret = cs_m68_read_calibration(cs, &observed);
		if (!ret && observed == desired)
			return 0;
		ret = cs_m68_set_calibration(cs, desired, 0);
		if (ret < 0)
			return ret;
		msleep(20);
	}
	/* The original silently succeeds after its fifth write. Verify it. */
	ret = cs_m68_read_calibration(cs, &observed);
	return ret < 0 ? ret : observed == desired ? 0 : -EIO;
}
