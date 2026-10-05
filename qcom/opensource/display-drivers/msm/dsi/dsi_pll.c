// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/unaligned.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include "dsi_pll.h"

static int dsi_pll_clock_register(struct platform_device *pdev,
				struct dsi_pll_resource *pll_res)
{
	int rc;

	switch (pll_res->pll_revision) {
	case DSI_PLL_5NM:
		rc = dsi_pll_clock_register_5nm(pdev, pll_res);
		break;
	case DSI_PLL_4NM:
		rc = dsi_pll_clock_register_4nm(pdev, pll_res);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc)
		DSI_PLL_ERR(pll_res, "clock register failed rc=%d\n", rc);

	return rc;
}

static inline int dsi_pll_get_ioresources(struct platform_device *pdev,
				void __iomem **regmap, char *resource_name)
{
	int rc = 0;
	struct resource *rsc = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, resource_name);
	if (rsc) {
		if (!regmap)
			return -ENOMEM;

		*regmap = devm_ioremap(&pdev->dev,
					rsc->start, resource_size(rsc));
		if (!*regmap)
			return -ENOMEM;
	}
	return rc;
}

static int dsi_pll_read_bootmem(struct device *dev, struct dfps_info *dfps)
{
	struct device_node *node;
	struct reserved_mem *mem;
	void *data;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node)
		return -ENOENT;
	mem = of_reserved_mem_lookup(node);
	of_node_put(node);
	if (!mem || mem->size < sizeof(*dfps))
		return -EINVAL;

	data = memremap(mem->base, sizeof(*dfps), MEMREMAP_WB);
	if (!data)
		return -ENOMEM;
	memcpy(dfps, data, sizeof(*dfps));
	memunmap(data);
	if (!dfps->vco_rate_cnt || dfps->vco_rate_cnt > ARRAY_SIZE(dfps->codes_dfps))
		return -EINVAL;
	return 0;
}

static int dsi_pll_read_dt_codes(struct device *dev, struct dfps_info *dfps)
{
	struct device_node *node;
	const __be32 *cells;
	struct pll_codes_info *info;
	struct pll_codes_header *header;
	unsigned int n, i;
	int len, ret = -EINVAL;

	node = of_parse_phandle(dev->of_node, "pll_codes_region", 0);
	if (!node)
		return -ENOENT;
	cells = of_get_property(node, "reg", &len);
	if (!cells || len < sizeof(struct pll_codes_header) || len % sizeof(*cells))
		goto put;

	/* Firmware packs little-endian records into the low bytes of DT cells. */
	n = be32_to_cpup(cells) >> 24;
	if (!n || ALIGN(struct_size(info, entries, n), sizeof(*cells)) > len)
		goto put;
	info = kzalloc(ALIGN(struct_size(info, entries, n), sizeof(*cells)), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto put;
	}
	for (i = 0; i < DIV_ROUND_UP(struct_size(info, entries, n), sizeof(*cells)); i++)
		put_unaligned_le32(be32_to_cpup(&cells[i]), (u8 *)info + i * sizeof(*cells));

	header = &info->header;
	if (le16_to_cpu(header->magic_id) != DSI_PLL_TRIM_CODES_MAGIC_ID ||
	    header->version < DSI_PLL_TRIM_CODES_VERSION ||
	    le16_to_cpu(header->size) != struct_size(info, entries, n))
		goto free;

	for (i = 0; i < n && dfps->vco_rate_cnt < ARRAY_SIZE(dfps->codes_dfps); i++) {
		const struct pll_codes_entry *src = &info->entries[i];
		struct dfps_codes_info *dst;

		if (src->device_id != DISPLAY_PLL_CODEID_DSI0 ||
		    src->num_codes < 3 || src->num_codes > ARRAY_SIZE(src->pll_codes))
			continue;
		dst = &dfps->codes_dfps[dfps->vco_rate_cnt++];
		dst->is_valid = 1;
		dst->clk_rate = le32_to_cpu(src->vco_rate);
		dst->pll_codes.pll_codes_1 = src->pll_codes[0];
		dst->pll_codes.pll_codes_2 = src->pll_codes[1];
		dst->pll_codes.pll_codes_3 = src->pll_codes[2];
	}
	ret = dfps->vco_rate_cnt ? 0 : -ENOENT;
free:
	kfree(info);
put:
	of_node_put(node);
	return ret;
}

int dsi_pll_init(struct platform_device *pdev, struct dsi_pll_resource **pll)
{
	int rc = 0;
	const char *label;
	struct dsi_pll_resource *pll_res = NULL;

	if (!pdev->dev.of_node) {
		pr_err("Invalid DSI PHY node\n");
		return -ENOTSUPP;
	}

	pll_res = devm_kzalloc(&pdev->dev, sizeof(struct dsi_pll_resource),
								GFP_KERNEL);
	if (!pll_res)
		return -ENOMEM;

	*pll = pll_res;

	label = of_get_property(pdev->dev.of_node, "pll-label", NULL);
	if (!label) {
		DSI_PLL_ERR(pll_res, "DSI pll label not specified\n");
		return 0;
	}

	DSI_PLL_INFO(pll_res, "DSI pll label = %s\n", label);


	if (!strcmp(label, "dsi_pll_4nm"))
		pll_res->pll_revision = DSI_PLL_4NM;
	else if (!strcmp(label, "dsi_pll_5nm"))
		pll_res->pll_revision = DSI_PLL_5NM;
	else
		return -ENOTSUPP;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index",
			&pll_res->index);
	if (rc) {
		DSI_PLL_ERR(pll_res, "Unable to get the cell-index rc=%d\n", rc);
		pll_res->index = 0;
	}

	pll_res->ssc_en = of_property_read_bool(pdev->dev.of_node,
						"qcom,dsi-pll-ssc-en");

	if (pll_res->ssc_en) {
		DSI_PLL_INFO(pll_res, "PLL SSC enabled\n");

		rc = of_property_read_u32(pdev->dev.of_node,
			"qcom,ssc-frequency-hz", &pll_res->ssc_freq);

		rc = of_property_read_u32(pdev->dev.of_node,
			"qcom,ssc-ppm", &pll_res->ssc_ppm);

		pll_res->ssc_center = false;

		label = of_get_property(pdev->dev.of_node,
			"qcom,dsi-pll-ssc-mode", NULL);

		if (label && !strcmp(label, "center-spread"))
			pll_res->ssc_center = true;
	}


	if (dsi_pll_get_ioresources(pdev, &pll_res->pll_base, "pll_base")) {
		DSI_PLL_ERR(pll_res, "Unable to remap pll base resources\n");
		return -ENOMEM;
	}

	pr_info("PLL base=%p\n", pll_res->pll_base);

	if (dsi_pll_get_ioresources(pdev, &pll_res->phy_base, "dsi_phy")) {
		DSI_PLL_ERR(pll_res, "Unable to remap pll phy base resources\n");
		return -ENOMEM;
	}

	if (dsi_pll_get_ioresources(pdev, &pll_res->dyn_pll_base,
							"dyn_refresh_base")) {
		DSI_PLL_ERR(pll_res, "Unable to remap dynamic pll base resources\n");
		return -ENOMEM;
	}

	if (dsi_pll_get_ioresources(pdev, &pll_res->gdsc_base, "gdsc_base"))
		DSI_PLL_DBG(pll_res, "Unable to remap gdsc base resources\n");

	pll_res->in_trusted_vm = of_property_read_bool(pdev->dev.of_node,
						"qcom,dsi-pll-in-trusted-vm");

	if (pll_res->in_trusted_vm) {
		DSI_PLL_INFO(pll_res,
			"Bypassing PLL clock register for Trusted VM\n");
		return rc;
	}

	rc = dsi_pll_clock_register(pdev, pll_res);
	if (rc) {
		DSI_PLL_ERR(pll_res, "clock register failed rc=%d\n", rc);
		return -EINVAL;
	}

	return rc;
}

void dsi_pll_parse_dfps_data(struct platform_device *pdev, struct dsi_pll_resource *pll_res)
{
	struct dfps_info dfps = {};
	int ret;

	if (pll_res->index || pll_res->in_trusted_vm || pll_res->dfps)
		return;
	ret = dsi_pll_read_dt_codes(&pdev->dev, &dfps);
	if (ret && dsi_pll_read_bootmem(&pdev->dev, &dfps))
		return;
	pll_res->dfps = devm_kmemdup(&pdev->dev, &dfps, sizeof(dfps), GFP_KERNEL);
}
