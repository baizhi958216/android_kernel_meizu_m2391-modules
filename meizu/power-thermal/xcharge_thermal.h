/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_XCHARGE_THERMAL_H
#define _MEIZU_XCHARGE_THERMAL_H
/* Original exported ABI, temperatures in hundredths of a degree Celsius. */
void xcharge_thermal_update_tbat(unsigned int tbat);
void xcharge_thermal_update_tusb(unsigned int tusb);
#endif
