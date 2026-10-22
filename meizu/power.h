/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MEIZU_POWER_RECOVERED_H
#define _MEIZU_POWER_RECOVERED_H

/* OEM power_supply protocol IDs recovered from xcharge_chg_manager split BTF.
 * They are wire values handled by the OEM power supply, not upstream IDs.
 */
enum mz_wireless_property_id {
	XCHARGE_WLS_PROP_MIN = 285,
	XCHARGE_WLS_RX_VOUT = XCHARGE_WLS_PROP_MIN,
	XCHARGE_WLS_RX_IOUT,
	XCHARGE_WLS_RX_POWER,
	XCHARGE_WLS_REV_CHG_STAT,
	XCHARGE_WLS_FW_UPDATE,
	XCHARGE_WLS_FW_VERIFY,
	XCHARGE_WLS_FW_VERSION,
	XCHARGE_WLS_CHG_LEVEL,
	XCHARGE_WLS_TX_MODEL_ID,
	XCHARGE_WLS_TX_FAN_MODE,
	XCHARGE_WLS_REG_ADDR,
	XCHARGE_WLS_REG_DATA,
	XCHARGE_WLS_BOOST_EN,
	XCHARGE_WLS_TX_COIL_ID,
	XCHARGE_WLS_TX_MAX_POWER,
	XCHARGE_WLS_ADAP_TYPE,
	XCHARGE_WLS_COIL_PWR_ON,
	XCHARGE_WLS_PROP_MAX,
};

/* Exact prototypes resolved using the original base and split BTF. */
int mpower_print(char *format, ...);
void xcharge_chg_interface_notify_rev_chg_sta(char *env_str[]);
void xcharge_chg_interface_notify_coil_pwr_on_sta(char *env_str[]);
void xcharge_wls_status_get_handle(void);

#endif
