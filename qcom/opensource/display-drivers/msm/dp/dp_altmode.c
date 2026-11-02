// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/soc/qcom/altmode-glink.h>
#include <linux/of_platform.h>
#include <linux/usb/dwc3-qcom.h>
#include <linux/usb/pd_vdo.h>

#include "dp_altmode.h"
#include "dp_debug.h"
#include "sde_dbg.h"


#define ALTMODE_CONFIGURE_MASK (0x3f)
#define ALTMODE_HPD_STATE_MASK (0x40)
#define ALTMODE_HPD_IRQ_MASK (0x80)

struct dp_altmode_private {
	bool forced_disconnect;
	struct device *dev;
	struct dp_hpd_cb *dp_cb;
	struct dp_altmode dp_altmode;
	struct altmode_client *amclient;
	bool connected;
	u32 lanes;
};

enum dp_altmode_pin_assignment {
	DPAM_HPD_OUT,
	DPAM_HPD_A,
	DPAM_HPD_B,
	DPAM_HPD_C,
	DPAM_HPD_D,
	DPAM_HPD_E,
	DPAM_HPD_F,
};

static int dp_altmode_set_usb_dp_mode(struct dp_altmode_private *altmode)
{
	int rc = 0;
	struct device_node *np;
	struct device_node *usb_node;
	struct platform_device *usb_pdev;
	enum typec_orientation orientation = TYPEC_ORIENTATION_NONE;
	int timeout = 250;

	if (!altmode || !altmode->dev) {
		DP_ERR("invalid args\n");
		return -EINVAL;
	}

	np = altmode->dev->of_node;

	usb_node = of_parse_phandle(np, "usb-controller", 0);
	if (!usb_node) {
		DP_ERR("unable to get usb node\n");
		return -EINVAL;
	}

	usb_pdev = of_find_device_by_node(usb_node);
	if (!usb_pdev) {
		of_node_put(usb_node);
		DP_ERR("unable to get usb pdev\n");
		return -EINVAL;
	}

	if (altmode->dp_altmode.base.orientation == ORIENTATION_CC1)
		orientation = TYPEC_ORIENTATION_NORMAL;
	else if (altmode->dp_altmode.base.orientation == ORIENTATION_CC2)
		orientation = TYPEC_ORIENTATION_REVERSE;
	while (timeout) {
		rc = dwc3_qcom_set_dp_lanes(&usb_pdev->dev,
					    altmode->connected ? altmode->lanes : 0,
					    orientation);
		if (rc != -EBUSY && rc != -EAGAIN)
			break;

		DP_WARN("USB busy, retry\n");

		/* wait for hw recommended delay for usb */
		msleep(20);
		timeout--;
	}
	of_node_put(usb_node);
	platform_device_put(usb_pdev);

	if (rc)
		DP_ERR("Error releasing SS lanes: %d\n", rc);

	return rc;
}

static int dp_altmode_notify(void *priv, void *data, size_t len)
{
	int rc = 0;
	struct dp_altmode_private *altmode =
			(struct dp_altmode_private *) priv;
	u8 port_index, dp_data, orientation;
	u8 *payload = (u8 *) data;
	u8 pin, hpd_state, hpd_irq;
	bool force_multi_func = altmode->dp_altmode.base.force_multi_func;

	port_index = payload[0];
	orientation = payload[1];
	dp_data = payload[8];

	pin = dp_data & ALTMODE_CONFIGURE_MASK;
	hpd_state = (dp_data & ALTMODE_HPD_STATE_MASK) >> 6;
	hpd_irq = (dp_data & ALTMODE_HPD_IRQ_MASK) >> 7;

	altmode->dp_altmode.base.hpd_high = !!hpd_state;
	altmode->dp_altmode.base.hpd_irq = !!hpd_irq;
	altmode->dp_altmode.base.multi_func = force_multi_func ? true :
		!(pin == DPAM_HPD_C || pin == DPAM_HPD_E || pin == DPAM_HPD_OUT);

	DP_DEBUG("payload=0x%x\n", dp_data);
	DP_DEBUG("port_index=%d, orientation=%d, pin=%d, hpd_state=%d\n",
			port_index, orientation, pin, hpd_state);
	DP_DEBUG("multi_func=%d, hpd_high=%d, hpd_irq=%d\n",
			altmode->dp_altmode.base.multi_func,
			altmode->dp_altmode.base.hpd_high,
			altmode->dp_altmode.base.hpd_irq);
	DP_DEBUG("connected=%d\n", altmode->connected);
	SDE_EVT32_EXTERNAL(dp_data, port_index, orientation, pin, hpd_state,
			altmode->dp_altmode.base.multi_func,
			altmode->dp_altmode.base.hpd_high,
			altmode->dp_altmode.base.hpd_irq, altmode->connected);

	if (!pin) {
		/* Cable detach */
		if (altmode->connected) {
			altmode->connected = false;
			altmode->dp_altmode.base.alt_mode_cfg_done = false;
			altmode->dp_altmode.base.orientation = ORIENTATION_NONE;
			if (altmode->dp_cb && altmode->dp_cb->disconnect)
				altmode->dp_cb->disconnect(altmode->dev);

			rc = dp_altmode_set_usb_dp_mode(altmode);
			if (rc)
				DP_ERR("failed to clear usb dp mode, rc: %d\n", rc);
		}
		return rc;
	}

	/* Configure */
	if (!altmode->connected) {
		altmode->connected = true;
		altmode->dp_altmode.base.alt_mode_cfg_done = true;
		altmode->forced_disconnect = false;
		altmode->lanes = 4;

		if (altmode->dp_altmode.base.multi_func)
			altmode->lanes = 2;

		DP_DEBUG("Connected=%d, lanes=%d\n",altmode->connected,altmode->lanes);

		switch (orientation) {
		case 0:
			orientation = ORIENTATION_CC1;
			break;
		case 1:
			orientation = ORIENTATION_CC2;
			break;
		case 2:
			orientation = ORIENTATION_NONE;
			break;
		default:
			orientation = ORIENTATION_NONE;
			break;
		}

		altmode->dp_altmode.base.orientation = orientation;

		rc = dp_altmode_set_usb_dp_mode(altmode);
		if (rc) {
			altmode->connected = false;
			altmode->dp_altmode.base.alt_mode_cfg_done = false;
			return rc;
		}

		if (altmode->dp_cb && altmode->dp_cb->configure)
			altmode->dp_cb->configure(altmode->dev);
		return rc;
	}

	/* Attention */
	if (altmode->forced_disconnect)
		return rc;

	if (altmode->dp_cb && altmode->dp_cb->attention)
		altmode->dp_cb->attention(altmode->dev);
	return rc;
}

static int dp_altmode_simulate_connect(struct dp_hpd *dp_hpd, bool hpd)
{
	struct dp_altmode *dp_altmode;
	struct dp_altmode_private *altmode;

	dp_altmode = container_of(dp_hpd, struct dp_altmode, base);
	altmode = container_of(dp_altmode, struct dp_altmode_private,
			dp_altmode);

	dp_altmode->base.hpd_high = hpd;
	altmode->forced_disconnect = !hpd;
	altmode->dp_altmode.base.alt_mode_cfg_done = hpd;

	if (hpd)
		altmode->dp_cb->configure(altmode->dev);
	else
		altmode->dp_cb->disconnect(altmode->dev);

	return 0;
}

static int dp_altmode_simulate_attention(struct dp_hpd *dp_hpd, int vdo)
{
	struct dp_altmode *dp_altmode;
	struct dp_altmode_private *altmode;
	struct dp_altmode *status;

	dp_altmode = container_of(dp_hpd, struct dp_altmode, base);
	altmode = container_of(dp_altmode, struct dp_altmode_private,
			dp_altmode);

	status = &altmode->dp_altmode;

	status->base.hpd_high  = (vdo & BIT(7)) ? true : false;
	status->base.hpd_irq   = (vdo & BIT(8)) ? true : false;

	if (altmode->dp_cb && altmode->dp_cb->attention)
		altmode->dp_cb->attention(altmode->dev);

	return 0;
}

struct dp_hpd *dp_altmode_get(struct device *dev, struct dp_hpd_cb *cb)
{
	struct altmode_client_data cd = {
		.name = "displayport",
		.svid = USB_SID_DISPLAYPORT,
		.callback = dp_altmode_notify,
	};
	struct dp_altmode_private *altmode;
	struct dp_altmode *dp_altmode;

	if (!cb) {
		DP_ERR("invalid cb data\n");
		return ERR_PTR(-EINVAL);
	}

	altmode = kzalloc(sizeof(*altmode), GFP_KERNEL);
	if (!altmode)
		return ERR_PTR(-ENOMEM);

	altmode->dev = dev;
	altmode->dp_cb = cb;

	dp_altmode = &altmode->dp_altmode;
	dp_altmode->base.register_hpd = NULL;
	dp_altmode->base.simulate_connect = dp_altmode_simulate_connect;
	dp_altmode->base.simulate_attention = dp_altmode_simulate_attention;

	cd.priv = altmode;
	altmode->amclient = altmode_register_client(dev, &cd);
	if (IS_ERR(altmode->amclient)) {
		int ret = PTR_ERR(altmode->amclient);

		kfree(altmode);
		return ERR_PTR(ret);
	}
	return &dp_altmode->base;
}

void dp_altmode_put(struct dp_hpd *dp_hpd)
{
	struct dp_altmode *dp_altmode;
	struct dp_altmode_private *altmode;

	dp_altmode = container_of(dp_hpd, struct dp_altmode, base);
	if (!dp_altmode)
		return;

	altmode = container_of(dp_altmode, struct dp_altmode_private,
			dp_altmode);

	altmode_deregister_client(altmode->amclient);

	kfree(altmode);
}
