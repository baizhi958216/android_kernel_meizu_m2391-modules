/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_XCHARGE_CHG_INTERFACE_H
#define _MEIZU_XCHARGE_CHG_INTERFACE_H
#include <linux/types.h>
enum discharging_voter {
	CMD_DISCHARGING,
	USB_CONNECTOR_HOT,
	USER_CHARGE_LIMIT,
	USER_WLS_CHARGE_LIMIT,
	DISCHARGING_VOTER_MAX,
};
void mz_chg_discharging_vote(enum discharging_voter voter, bool value);
void xcharge_chg_interface_register_cd(void);
void xcharge_chg_interface_notify_rev_chg_sta(char *env_str[]);
void xcharge_chg_interface_notify_coil_pwr_on_sta(char *env_str[]);
#endif
