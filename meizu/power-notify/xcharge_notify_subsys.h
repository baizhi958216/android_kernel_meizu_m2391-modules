/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_XCHARGE_NOTIFY_SUBSYS_H
#define _MEIZU_XCHARGE_NOTIFY_SUBSYS_H

#include <linux/notifier.h>
#include <linux/types.h>

#include <linux/meizu_power.h>

enum chg_sta {
	WIRE_CHARGING = 0,
	WIRE_NCHARGING = 1,
	WLS_CHARGING = 2,
	WLS_NCHARGING = 3,
};

int register_mpower_notifier(struct notifier_block *nb);
int unregister_mpower_notifier(struct notifier_block *nb);
/* Enum spelling/order and the bool typedef reproduce the original setter's
 * genksyms CRC 0x98fe0097. Keep implicit enum values: explicit initializers
 * change genksyms' type tokens even though the machine values are identical.
 */
void xcharge_notify_subsys_set_plug_status(
		enum xcharge_notify_subsys_plug_type type, bool plugged);

#endif
