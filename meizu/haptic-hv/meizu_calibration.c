// SPDX-License-Identifier: GPL-2.0-only
/* M2391 slot format recovered from aw8697ReadFreqDataFormSlot/GetFreqThenSave. */
#include <linux/errno.h>
#include <linux/kernel.h>
#include "../storage/meizu_storage.h"
#include "meizu_calibration.h"

#define MZ_HAPTIC_CALIBRATION_SLOT 72
#define MZ_HAPTIC_CALIBRATION_BYTES 64

int mz_haptic_read_calibration(struct mz_haptic_calibration *record)
{
	struct mz_haptic_calibration parsed;
	char buf[MZ_HAPTIC_CALIBRATION_BYTES + 1] = { 0 };
	int ret;
	int i;

	if (!record)
		return -EINVAL;
	ret = private_slot_rw(0, MZ_HAPTIC_CALIBRATION_SLOT, buf,
			MZ_HAPTIC_CALIBRATION_BYTES);
	if (ret < 0)
		return ret;
	for (i = 0; i < MZ_HAPTIC_CALIBRATION_BYTES; i++)
		if (buf[i] && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n' &&
		    buf[i] != '\r' && buf[i] != '\f' && buf[i] != '\v')
			break;
	/* The original zero-initializes all eight parsed integers, allowing a
	 * fresh blank slot to be provisioned. Distinguish it from corrupt data.
	 */
	if (i == MZ_HAPTIC_CALIBRATION_BYTES)
		return -ENODATA;
	/* Keep the extra terminator outside the original 64-byte I/O. A partial
	 * record must never be used to silently overwrite calibration fields.
	 */
	ret = sscanf(buf, "%d %d %d %d %d %d %d %d",
		&parsed.selected_chip, &parsed.manual_f0,
		&parsed.factory_f0_first, &parsed.factory_f0_second,
		&parsed.factory_trim, &parsed.user_f0_first,
		&parsed.user_f0_second, &parsed.user_trim);
	if (ret != 8)
		return -EINVAL;
	*record = parsed;
	return 0;
}

int mz_haptic_write_calibration(const struct mz_haptic_calibration *record)
{
	char buf[MZ_HAPTIC_CALIBRATION_BYTES] = { 0 };
	int ret;

	if (!record)
		return -EINVAL;
	ret = snprintf(buf, sizeof(buf), "%d %d %d %d %d %d %d %d",
		record->selected_chip, record->manual_f0,
		record->factory_f0_first, record->factory_f0_second,
		record->factory_trim, record->user_f0_first,
		record->user_f0_second, record->user_trim);
	if (ret < 0)
		return ret;
	if (ret >= sizeof(buf))
		return -EOVERFLOW;
	ret = private_slot_rw(1, MZ_HAPTIC_CALIBRATION_SLOT, buf, sizeof(buf));
	return ret < 0 ? ret : 0;
}
