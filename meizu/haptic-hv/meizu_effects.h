/* SPDX-License-Identifier: GPL-2.0-only */
/* Exact M2391 RAM/RTP effect IDs and payloads recovered from haptic_hv.ko. */
#ifndef _MEIZU_HAPTIC_EFFECTS_H
#define _MEIZU_HAPTIC_EFFECTS_H

#include <linux/kernel.h>
#include <linux/types.h>

struct mz_haptic_ram_effect {
	int id;
	u8 sequence[8];
	u8 loop[5];
};

struct mz_haptic_rtp_effect {
	int id;
	const char *filename;
};

static struct mz_haptic_ram_effect mz_haptic_ram_effects[] = {
	{ 20120, { 8, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 20200, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 20300, { 20, 18, 15, 0, 0, 0, 0, 0 }, { 8, 0, 0, 0, 0 } },
	{ 21000, { 11, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 21020, { 8, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 21230, { 9, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 21240, { 9, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 21251, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 21600, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 21900, { 9, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22140, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22160, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22500, { 12, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22502, { 12, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22507, { 12, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22509, { 12, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22510, { 4, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22520, { 11, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22550, { 12, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 22560, { 12, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 25160, { 4, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 25550, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 25688, { 11, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 30200, { 17, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 30201, { 5, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 30203, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 30500, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 30800, { 10, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 30900, { 3, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31001, { 17, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31003, { 129, 3, 139, 3, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31004, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31005, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31006, { 3, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31007, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31008, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31009, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31010, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31011, { 7, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31012, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31013, { 129, 3, 139, 3, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31014, { 10, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31015, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31016, { 8, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31017, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31018, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31019, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31020, { 8, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31021, { 8, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31022, { 13, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31023, { 5, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31024, { 7, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31025, { 11, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
	{ 31026, { 8, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
};

static const struct mz_haptic_rtp_effect mz_haptic_rtp_effects[] = {
	{ 5000, "sim_ring00.bin" },
	{ 5001, "sim_ring01.bin" },
	{ 5002, "sim_ring02.bin" },
	{ 5003, "sim_ring03.bin" },
	{ 5004, "sim_ring04.bin" },
	{ 5005, "sim_ring05.bin" },
	{ 5006, "sim_ring06.bin" },
	{ 5007, "sim_ring07.bin" },
	{ 5008, "sim_ring08.bin" },
	{ 5009, "sim_ring09.bin" },
	{ 5010, "sim_ring10.bin" },
	{ 5011, "sim_ring11.bin" },
	{ 5012, "sim_ring12.bin" },
	{ 5013, "sim_ring13.bin" },
	{ 5014, "sim_ring14.bin" },
	{ 5015, "sim_ring15.bin" },
	{ 5016, "sim_ring16.bin" },
	{ 5017, "sim_ring17.bin" },
	{ 5018, "sim_ring18.bin" },
	{ 5019, "sim_ring19.bin" },
	{ 5020, "sim_ring20.bin" },
	{ 5021, "sim_ring21.bin" },
	{ 5022, "sim_ring22.bin" },
	{ 5023, "sim_ring23.bin" },
	{ 5024, "sim_ring24.bin" },
	{ 5025, "sim_ring25.bin" },
	{ 5026, "sim_ring26.bin" },
	{ 5027, "sim_ring27.bin" },
	{ 5028, "sim_ring28.bin" },
	{ 5029, "sim_ring29.bin" },
	{ 5030, "sim_ring30.bin" },
	{ 5031, "sim_ring31.bin" },
	{ 5032, "sim_ring32.bin" },
	{ 5033, "sim_ring33.bin" },
	{ 5034, "sim_ring34.bin" },
	{ 5035, "sim_ring35.bin" },
	{ 5036, "sim_ring36.bin" },
	{ 5037, "sim_ring37.bin" },
	{ 5038, "sim_ring38.bin" },
	{ 5039, "sim_ring39.bin" },
	{ 5040, "sim_ring40.bin" },
	{ 5041, "defsim_ring.bin" },
	{ 5042, "defalm_ring.bin" },
	{ 5043, "sms_ring01.bin" },
	{ 5044, "sms_ring02.bin" },
	{ 5045, "sms_ring03.bin" },
	{ 5046, "sms_ring04.bin" },
	{ 5047, "sms_ring05.bin" },
	{ 5048, "sms_ring06.bin" },
	{ 5049, "sms_ring07.bin" },
	{ 5050, "sms_ring08.bin" },
	{ 5051, "sms_ring09.bin" },
	{ 5052, "sms_ring10.bin" },
	{ 5053, "sms_ring11.bin" },
	{ 5054, "sms_ring12.bin" },
	{ 5055, "sms_ring13.bin" },
	{ 5056, "sms_ring14.bin" },
	{ 5057, "sms_ring15.bin" },
	{ 5058, "sms_ring16.bin" },
	{ 5059, "sms_ring17.bin" },
	{ 5060, "sms_ring18.bin" },
	{ 5061, "sms_ring19.bin" },
	{ 5062, "sms_ring20.bin" },
	{ 5063, "sms_ring21.bin" },
	{ 5064, "sms_ring22.bin" },
	{ 5065, "sms_ring23.bin" },
	{ 5066, "sms_ring24.bin" },
	{ 5067, "sms_ring25.bin" },
	{ 5068, "sms_ring26.bin" },
	{ 5069, "sms_ring27.bin" },
	{ 5070, "sms_ring28.bin" },
	{ 5071, "sms_ring29.bin" },
	{ 5072, "sms_ring30.bin" },
	{ 5073, "sms_ring31.bin" },
	{ 5074, "sms_ring32.bin" },
	{ 5100, "mpacc_ring.bin" },
	{ 5101, "plug_usb_ring.bin" },
	{ 5102, "cleanup_ring.bin" },
	{ 5103, "soft_alarm.bin" },
	{ 5104, "defalm_ring01.bin" },
	{ 5105, "coin_ring.bin" },
	{ 5106, "mute_notifi.bin" },
	{ 5107, "sim_ring41.bin" },
	{ 5108, "sim_ring42.bin" },
	{ 5109, "defalm_ring02.bin" },
	{ 5110, "soft_alarm01.bin" },
};

/* Original interpolation starts with high=77, but can select above that
 * bound and reaches all 86 valid IDs. Use a bounded search for that same
 * set instead of allowing malformed IDs to trigger its UBSAN bounds trap.
 */
static inline int mz_haptic_find_rtp_effect(unsigned int id)
{
	int low = 0, high = ARRAY_SIZE(mz_haptic_rtp_effects) - 1;

	while (low <= high) {
		int mid = low + (high - low) / 2;
		unsigned int effect_id = mz_haptic_rtp_effects[mid].id;

		if (effect_id == id)
			return mid;
		if (effect_id < id)
			low = mid + 1;
		else
			high = mid - 1;
	}
	return -1;
}

static inline int mz_haptic_find_ram_effect(unsigned int id)
{
	int low = 0, high = ARRAY_SIZE(mz_haptic_ram_effects) - 1;

	while (low <= high) {
		int mid = low + (high - low) / 2;
		unsigned int effect_id = mz_haptic_ram_effects[mid].id;

		if (effect_id == id)
			return mid;
		if (effect_id < id)
			low = mid + 1;
		else
			high = mid - 1;
	}
	return -1;
}

#endif
