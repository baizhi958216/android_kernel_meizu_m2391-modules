/* SPDX-License-Identifier: GPL-2.0-only */
/* Names, modes and callback presence recovered from original .data. */
static struct mz_sysfs_attr usb_c_attrs[] = {
	{ .kattr = { .attr = { .name = "plug_orient", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "pps_online", .mode = 0444 }, .show = forward_show, .store = NULL } },
};
static struct mz_sysfs_attr wired_attrs[] = {
	{ .kattr = { .attr = { .name = "wired_level", .mode = 0644 }, .show = forward_show, .store = forward_store } },
	{ .kattr = { .attr = { .name = "cmd_discharging", .mode = 0644 }, .show = forward_show, .store = forward_store } },
	{ .kattr = { .attr = { .name = "discharge_cur", .mode = 0444 }, .show = forward_show, .store = NULL } },
};
static struct mz_sysfs_attr battery_attrs[] = {
	{ .kattr = { .attr = { .name = "soh", .mode = 0444 }, .show = forward_show, .store = NULL } },
};
static struct mz_sysfs_attr wireless_attrs[] = {
	{ .kattr = { .attr = { .name = "reverse_chg_vout", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "reverse_chg_iout", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "vout", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "iout", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "power", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "reverse_chg_stat", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "fw_update", .mode = 0664 }, .show = NULL, .store = forward_store } },
	{ .kattr = { .attr = { .name = "fw_verify", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "fw_version", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "wls_level", .mode = 0664 }, .show = NULL, .store = forward_store } },
	{ .kattr = { .attr = { .name = "txid", .mode = 0444 }, .show = forward_show, .store = NULL } },
	{ .kattr = { .attr = { .name = "tx_fan_mode", .mode = 0664 }, .show = NULL, .store = forward_store } },
	{ .kattr = { .attr = { .name = "rx_reg_addr", .mode = 0664 }, .show = forward_show, .store = forward_store } },
	{ .kattr = { .attr = { .name = "rx_reg_data", .mode = 0664 }, .show = forward_show, .store = forward_store } },
	{ .kattr = { .attr = { .name = "reverse_chg_enable", .mode = 0664 }, .show = NULL, .store = forward_store } },
};

static const struct forwarding_attr forwarding_attrs[] = {
	{ &usb_c_attrs[0].kattr, "usb", 285, "%d\n", 0, 0 }, /* plug_orient */
	{ &usb_c_attrs[1].kattr, "usb", 64, "%d\n", 5, 0 }, /* pps_online */
	{ &wired_attrs[0].kattr, "battery", 306, "%d\n", 1, 0 }, /* wired_level */
	{ &wired_attrs[1].kattr, "battery", 305, "%d\n", 3, 0 }, /* cmd_discharging */
	{ &wired_attrs[2].kattr, "battery", 309, "%d\n", 0, 0 }, /* discharge_cur */
	{ &battery_attrs[0].kattr, "battery", 50, "%d\n", 0, 0 }, /* soh */
	{ &wireless_attrs[0].kattr, "wireless", 12, "%d\n", 0, 0 }, /* reverse_chg_vout */
	{ &wireless_attrs[1].kattr, "wireless", 17, "%d\n", 0, 0 }, /* reverse_chg_iout */
	{ &wireless_attrs[2].kattr, "wireless", 285, "%d\n", 0, 0 }, /* vout */
	{ &wireless_attrs[3].kattr, "wireless", 286, "%d\n", 0, 0 }, /* iout */
	{ &wireless_attrs[4].kattr, "wireless", 287, "%d\n", 0, 0 }, /* power */
	{ &wireless_attrs[5].kattr, "wireless", 288, "%d\n", 4, 0 }, /* reverse_chg_stat */
	{ &wireless_attrs[6].kattr, "wireless", 289, "%d", 0, 0 }, /* fw_update */
	{ &wireless_attrs[7].kattr, "wireless", 290, "%d\n", 0, 0 }, /* fw_verify */
	{ &wireless_attrs[8].kattr, "wireless", 291, "%#x\n", 0, 0 }, /* fw_version */
	{ &wireless_attrs[9].kattr, "wireless", 292, "%d", 2, 0 }, /* wls_level */
	{ &wireless_attrs[10].kattr, "wireless", 293, "WIRELESS_TX_MODEL_M%x\n", 0, 0 }, /* txid */
	{ &wireless_attrs[11].kattr, "wireless", 294, "%d", 0, 0 }, /* tx_fan_mode */
	{ &wireless_attrs[12].kattr, "wireless", 295, "0x%x\n", 0, 1 }, /* rx_reg_addr */
	{ &wireless_attrs[13].kattr, "wireless", 296, "0x%x\n", 0, 1 }, /* rx_reg_data */
	{ &wireless_attrs[14].kattr, "wireless", 297, "%d", 0, 0 }, /* reverse_chg_enable */
};
