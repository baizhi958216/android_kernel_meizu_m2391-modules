/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_MBATTERY_LOGGER_H
#define _MEIZU_MBATTERY_LOGGER_H
#include <linux/meizu_power.h>

void xcharge_mbattery_logger_update_plug_in_status(enum mbattery_plug_in_type type,
		int status);
#endif
