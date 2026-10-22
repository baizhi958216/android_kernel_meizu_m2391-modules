/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DSI_MEIZU_H
#define DSI_MEIZU_H
#include <linux/types.h>
struct dsi_display;
struct dsi_panel;
struct dsi_cmd_desc;
struct kobject;
struct drm_connector;
int dsi_meizu_factory_init(struct dsi_display *display, struct kobject *kobj);
void dsi_meizu_factory_remove(struct dsi_display *display, struct kobject *kobj);
void dsi_meizu_capture_pps(struct dsi_panel *panel, struct dsi_cmd_desc *cmd);
bool dsi_meizu_log_enabled(u32 mask);
void dsi_meizu_log_command(struct dsi_cmd_desc *cmd);
void dsi_meizu_connector_remove(struct drm_connector *connector);
#endif
