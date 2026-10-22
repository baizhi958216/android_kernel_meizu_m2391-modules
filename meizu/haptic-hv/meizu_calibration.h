/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_HAPTIC_CALIBRATION_H
#define _MEIZU_HAPTIC_CALIBRATION_H

/* Slot 72 contains eight space-separated decimal integers in 64 bytes. */
struct mz_haptic_calibration {
	int selected_chip;
	int manual_f0;
	int factory_f0_first;
	int factory_f0_second;
	int factory_trim;
	int user_f0_first;
	int user_f0_second;
	int user_trim;
};

int mz_haptic_read_calibration(struct mz_haptic_calibration *record);
int mz_haptic_write_calibration(const struct mz_haptic_calibration *record);

#endif
