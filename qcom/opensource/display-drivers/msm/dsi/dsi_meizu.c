// SPDX-License-Identifier: GPL-2.0-only
/* M2391 /sys/kernel/display_drivers factory protocol recovered from msm_drm.ko. */
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/completion.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_meizu.h"
#include "sde_connector.h"

extern const char *cmd_set_prop_map[DSI_CMD_SET_MAX];
struct meizu_panel_factory {
	struct dsi_display *display;
	struct kref references;
	struct completion drained;
	struct mutex lock;
	spinlock_t pps_lock;
	u8 pending[4096], registers[256];
	u32 pending_size;
	u8 register_count;
	int scene, log_mask;
	bool pending_lp, group_created, link_created;
	char pps[512];
};
static struct meizu_panel_factory *meizu_factory;
static DEFINE_SPINLOCK(meizu_factory_registry_lock);
static atomic_t meizu_dsi_log_mask = ATOMIC_INIT(0);

bool dsi_meizu_log_enabled(u32 mask)
{
	return !!(atomic_read(&meizu_dsi_log_mask) & mask);
}

void dsi_meizu_log_command(struct dsi_cmd_desc *cmd)
{
	pr_info("[LCD] DSI %02x 00 %02x %02x %02x %02x %02x %*ph\n",
		cmd->msg.type, cmd->msg.channel, cmd->last_command ? 0 : 0x40,
		cmd->post_wait_ms, (u32)cmd->msg.tx_len >> 8,
		(u32)cmd->msg.tx_len & 0xff,
		(int)min_t(size_t, cmd->msg.tx_len, 256), cmd->msg.tx_buf);
}


static int meizu_flush_commands(struct meizu_panel_factory *factory)
{
	struct dsi_display *display = factory->display;
	struct dsi_panel_cmd_set replacement = { 0 }, *old;
	const u8 *data = factory->pending;
	u32 left = factory->pending_size, count = 0, i, size;
	int rc = -EINVAL;

	if (!left) return 0;
	if (factory->scene < 0 || factory->scene >= DSI_CMD_SET_MAX) return -EINVAL;
	/* Validate the complete stream before changing the live command set. */
	while (left) {
		if (left < 7) return -EINVAL;
		size = ((u32)data[5] << 8) | data[6];
		if (size > left - 7) return -EINVAL;
		left -= size + 7;
		data += size + 7;
		count++;
	}
	replacement.cmds = kcalloc(count, sizeof(*replacement.cmds), GFP_KERNEL);
	if (!replacement.cmds) return -ENOMEM;
	replacement.count = count;
	data = factory->pending;
	for (i = 0; i < count; i++) {
		struct dsi_cmd_desc *cmd = &replacement.cmds[i];
		size = ((u32)data[5] << 8) | data[6];
		cmd->msg.type = data[0];
		cmd->msg.channel = data[2];
		cmd->msg.flags = data[3] == 1 ? MIPI_DSI_MSG_REQ_ACK : 0;
		cmd->last_command = !(data[3] & BIT(6));
		cmd->post_wait_ms = data[4];
		cmd->msg.tx_len = size;
		cmd->msg.tx_buf = kmemdup(data + 7, size, GFP_KERNEL);
		if (size && !cmd->msg.tx_buf) { rc = -ENOMEM; goto free_commands; }
		data += size + 7;
	}
	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);
	if (!display->panel->cur_mode || !display->panel->cur_mode->priv_info) goto unlock;
	old = &display->panel->cur_mode->priv_info->cmd_sets[factory->scene];
	replacement.type = old->type;
	/* Stock HS command keeps the existing state; LP explicitly sets zero. */
	replacement.state = factory->pending_lp ? DSI_CMD_SET_STATE_LP : old->state;
	dsi_panel_destroy_cmd_packets(old);
	dsi_panel_dealloc_cmd_packets(old);
	*old = replacement;
	replacement.cmds = NULL;
	factory->pending_size = 0;
	factory->pending_lp = false;
	rc = 0;
unlock:
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
free_commands:
	if (replacement.cmds) {
		for (i = 0; i < count; i++) kfree(replacement.cmds[i].msg.tx_buf);
		kfree(replacement.cmds);
	}
	return rc;
}

static void meizu_dump_commands(struct meizu_panel_factory *factory)
{
	struct dsi_display *display = factory->display;
	struct dsi_panel_cmd_set *set;
	u32 i, j;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);
	if (!display->panel->cur_mode || !display->panel->cur_mode->priv_info) goto out;
	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		if (!cmd_set_prop_map[i]) continue;
		set = &display->panel->cur_mode->priv_info->cmd_sets[i];
		pr_info("[display:sh]%s: %s\n", cmd_set_prop_map[i],
			set->state == DSI_CMD_SET_STATE_LP ? "dsi_lp_mode" : "dsi_hs_mode");
		for (j = 0; j < set->count; j++) {
			struct dsi_cmd_desc *cmd = &set->cmds[j];
			pr_info("[display:sh] %02x 00 %02x %02x %02x %02x %02x %*ph\n",
				cmd->msg.type, cmd->msg.channel, cmd->last_command ? 0 : 0x40,
				cmd->post_wait_ms, (u32)cmd->msg.tx_len >> 8,
				(u32)cmd->msg.tx_len & 0xff,
				(int)min_t(size_t, cmd->msg.tx_len, 256), cmd->msg.tx_buf);
		}
	}
out:
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
}

static int meizu_factory_commands(struct meizu_panel_factory *factory, const char *buf)
{
	char token[256];
	const char *cursor = buf;
	u8 *bytes;
	u32 value, length = 0;
	int i, consumed, rc;

	if (sscanf(buf, "%255s", token) != 1) return -EINVAL;
	if (!strcmp(token, "dump")) { meizu_dump_commands(factory); return 0; }
	if (!strcmp(token, "dsi_lp_mode")) { factory->pending_lp = true; return 0; }
	if (!strcmp(token, "dsi_hs_mode")) { factory->pending_lp = false; return 0; }
	if (!strcmp(token, "flush")) return meizu_flush_commands(factory);
	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		if (cmd_set_prop_map[i] && !strcmp(token, cmd_set_prop_map[i])) {
			rc = meizu_flush_commands(factory);
			if (!rc) factory->scene = i;
			return rc;
		}
	}
	bytes = kmalloc(4096, GFP_KERNEL);
	if (!bytes) return -ENOMEM;
	while (sscanf(cursor, "%x%n", &value, &consumed) == 1) {
		if (value > 255 || length >= 4096) { rc = -EINVAL; goto out; }
		bytes[length++] = value;
		cursor += consumed;
	}
	if (*skip_spaces(cursor) || !length || factory->pending_size + length >= sizeof(factory->pending)) { rc = -EINVAL; goto out; }
	memcpy(factory->pending + factory->pending_size, bytes, length);
	factory->pending_size += length;
	rc = 0;
out:
	kfree(bytes);
	return rc;
}

static int meizu_factory_registers(struct meizu_panel_factory *factory, const char *buf)
{
	struct dsi_display *display = factory->display;
	u8 data[256] = { 0 }, packet[] = { 6, 0, 0, 0, 0, 0, 1, 0 };
	const char *cursor = buf;
	u32 address, value, power, length = 0;
	int read_length, consumed, rc;
	bool clocks;

	display_get_power_status(&power);
	if (!power) return -EFAULT;
	if (buf[0] == 'r') {
		if (sscanf(buf + 1, "%x %d", &address, &read_length) != 2 ||
			address > 255 || read_length < 1 || read_length > sizeof(data)) return -EINVAL;
		packet[7] = address;
		rc = dsi_display_cmd_receive(display, packet, sizeof(packet), data, read_length);
		if (rc < 1) return -EINVAL;
		memcpy(factory->registers, data, sizeof(data));
		factory->register_count = read_length; /* Original cache count is u8. */
		return 0;
	}
	while (sscanf(cursor, "%x%n", &value, &consumed) == 1) {
		if (length >= sizeof(data) || value > 255) return -EINVAL;
		data[length++] = value;
		cursor += consumed;
	}
	if (*skip_spaces(cursor) || !length) return -EINVAL;
	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);
	if (!display->panel->panel_initialized) { rc = -1; goto out; }
	clocks = display->config.panel_mode == DSI_OP_CMD_MODE;
	if (clocks) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_ON);
		if (rc) goto out;
	}
	rc = mipi_dsi_dcs_write(&display->panel->mipi_device, data[0], data + 1, length - 1);
	if (clocks) dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_OFF);
out:
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
	return rc < 0 ? rc : 0;
}

static ssize_t meizu_factory_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct meizu_panel_factory *factory = READ_ONCE(meizu_factory);
	struct dsi_display *display;
	unsigned long flags;
	size_t length = 0;
	int i;

	if (!factory) return -ENODEV;
	display = factory->display;
	mutex_lock(&factory->lock);
	if (!strcmp(attr->attr.name, "panel_info"))
		length = sysfs_emit(buf, "%s\n", display->panel->name);
	else if (!strcmp(attr->attr.name, "panel_pps")) {
		spin_lock_irqsave(&factory->pps_lock, flags);
		length = sysfs_emit(buf, "%s\n", factory->pps);
		spin_unlock_irqrestore(&factory->pps_lock, flags);
	} else if (!strcmp(attr->attr.name, "dsi_log_switch"))
		length = sysfs_emit(buf, "%d\n", factory->log_mask);
	else if (!strcmp(attr->attr.name, "esd_status")) {
		mutex_lock(&display->display_lock);
		length = sysfs_emit(buf, "%d\n", display->panel->esd_config.esd_enabled);
		mutex_unlock(&display->display_lock);
	} else if (!strcmp(attr->attr.name, "write_panel_reg")) {
		for (i = 0; i < factory->register_count; i++)
			length += sysfs_emit_at(buf, length, "%02x ", factory->registers[i]);
		length += sysfs_emit_at(buf, length, "\n");
	} else {
		length = sysfs_emit(buf, "read current dsi_cmd:\n\techo dump > dsi_cmd  - then you can find dsi cmd on kmsg\nset sence dsi cmd:\n  example hbm on:\n\techo qcom,mdss-dsi-hbm-on-command > dsi_cmd\n\techo [dsi cmd0] > dsi_cmd\n\techo [dsi cmd1] > dsi_cmd\n\techo [dsi cmdX] > dsi_cmd\n\techo flush > dsi_cmd\navailable dsi_cmd sences:\n");
		for (i = 0; i < DSI_CMD_SET_MAX && length < PAGE_SIZE; i++)
			if (cmd_set_prop_map[i]) length += sysfs_emit_at(buf, length, "%s\n", cmd_set_prop_map[i]);
	}
	mutex_unlock(&factory->lock);
	return length;
}

static ssize_t meizu_factory_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct meizu_panel_factory *factory = READ_ONCE(meizu_factory);
	struct dsi_display *display;
	int value, rc = 0;

	if (!factory) return -ENODEV;
	display = factory->display;
	mutex_lock(&factory->lock);
	if (!strcmp(attr->attr.name, "dsi_cmd")) rc = meizu_factory_commands(factory, buf);
	else if (!strcmp(attr->attr.name, "write_panel_reg"))
		rc = meizu_factory_registers(factory, buf);
	else if (sscanf(buf, "%d", &value) != 1) rc = -EINVAL;
	else if (!strcmp(attr->attr.name, "dsi_log_switch")) {
		factory->log_mask = value;
		atomic_set(&meizu_dsi_log_mask, value);
		pr_info("[LCD] DSI factory log mask=%d\n", value);
	} else if (!display->drm_conn) rc = -EINVAL;
	else if (!!value != display->panel->esd_config.esd_enabled) {
		/* The existing connector helper owns work scheduling/cancellation.
		 * Do not hold display_lock while waiting for its ESD worker.
		 */
		if (value) WRITE_ONCE(display->panel->esd_config.esd_enabled, true);
		sde_connector_schedule_status_work(display->drm_conn, !!value);
		WRITE_ONCE(display->panel->esd_config.esd_enabled, !!value);
		atomic_set(&display->panel->esd_recovery_pending, !value);
	}
	mutex_unlock(&factory->lock);
	return rc < 0 ? rc : count;
}

#define MEIZU_PANEL_ATTRIBUTE(name, store) \
	static struct kobj_attribute meizu_attr_##name = __ATTR(name, 0644, meizu_factory_show, store)
MEIZU_PANEL_ATTRIBUTE(panel_info, NULL);
MEIZU_PANEL_ATTRIBUTE(panel_pps, NULL);
MEIZU_PANEL_ATTRIBUTE(dsi_log_switch, meizu_factory_store);
MEIZU_PANEL_ATTRIBUTE(esd_status, meizu_factory_store);
MEIZU_PANEL_ATTRIBUTE(write_panel_reg, meizu_factory_store);
MEIZU_PANEL_ATTRIBUTE(dsi_cmd, meizu_factory_store);
static struct attribute *meizu_factory_attrs[] = {
	&meizu_attr_panel_info.attr, &meizu_attr_panel_pps.attr,
	&meizu_attr_dsi_log_switch.attr, &meizu_attr_esd_status.attr,
	&meizu_attr_write_panel_reg.attr, &meizu_attr_dsi_cmd.attr, NULL,
};
static const struct attribute_group meizu_factory_group = { .attrs = meizu_factory_attrs };

/* Caller holds its panel_lock, which may belong to a secondary panel.
 * Registry protection therefore covers lookup/comparison and the complete
 * cache access, independently of which panel is sending PPS. */
void dsi_meizu_capture_pps(struct dsi_panel *panel, struct dsi_cmd_desc *cmd)
{
	struct meizu_panel_factory *factory;
	unsigned long flags;
	size_t length;
	u32 i;

	spin_lock_irqsave(&meizu_factory_registry_lock, flags);
	factory = meizu_factory;
	if (!factory || factory->display->panel != panel) goto out;
	spin_lock(&factory->pps_lock);
	length = scnprintf(factory->pps, sizeof(factory->pps),
		"%02X 00 %02X %02X %02X %02X %02X ", cmd->msg.type, cmd->msg.channel,
		cmd->last_command ? 0 : 0x40, cmd->post_wait_ms,
		(u32)cmd->msg.tx_len >> 8, (u32)cmd->msg.tx_len & 0xff);
	for (i = 0; i < cmd->msg.tx_len && length < sizeof(factory->pps) - 1; i++)
		length += scnprintf(factory->pps + length, sizeof(factory->pps) - length,
			"%02X ", ((const u8 *)cmd->msg.tx_buf)[i]);
	spin_unlock(&factory->pps_lock);
out:
	spin_unlock_irqrestore(&meizu_factory_registry_lock, flags);
}

static void meizu_factory_reference_release(struct kref *reference)
{
	struct meizu_panel_factory *factory =
		container_of(reference, struct meizu_panel_factory, references);

	complete(&factory->drained);
}

/* Called before connector destruction cancels its ESD work. Serialize with
 * ESD sysfs, then unpublish the connector so no later callback can rearm it.
 * Remove drains this reference before the display can be freed.
 */
void dsi_meizu_connector_remove(struct drm_connector *connector)
{
	struct meizu_panel_factory *factory;
	unsigned long flags;

	spin_lock_irqsave(&meizu_factory_registry_lock, flags);
	factory = meizu_factory;
	if (factory) kref_get(&factory->references);
	spin_unlock_irqrestore(&meizu_factory_registry_lock, flags);
	if (!factory) return;
	mutex_lock(&factory->lock);
	if (factory->display->drm_conn == connector)
		WRITE_ONCE(factory->display->drm_conn, NULL);
	mutex_unlock(&factory->lock);
	kref_put(&factory->references, meizu_factory_reference_release);
}

int dsi_meizu_factory_init(struct dsi_display *display, struct kobject *kobj)
{
	struct meizu_panel_factory *factory;
	unsigned long flags;
	int rc;

	factory = kzalloc(sizeof(*factory), GFP_KERNEL);
	if (!factory) return -ENOMEM;
	factory->display = display;
	factory->scene = DSI_CMD_SET_MAX;
	kref_init(&factory->references);
	init_completion(&factory->drained);
	mutex_init(&factory->lock);
	spin_lock_init(&factory->pps_lock);
	spin_lock_irqsave(&meizu_factory_registry_lock, flags);
	if (meizu_factory) {
		spin_unlock_irqrestore(&meizu_factory_registry_lock, flags);
		kfree(factory);
		return -EBUSY;
	}
	WRITE_ONCE(meizu_factory, factory);
	spin_unlock_irqrestore(&meizu_factory_registry_lock, flags);
	rc = sysfs_create_group(kobj, &meizu_factory_group);
	if (!rc) {
		factory->group_created = true;
		rc = sysfs_create_link(kobj, &display->pdev->dev.kobj, "panel");
		if (!rc) factory->link_created = true;
	}
	if (rc) dsi_meizu_factory_remove(display, kobj);
	return rc;
}

void dsi_meizu_factory_remove(struct dsi_display *display, struct kobject *kobj)
{
	struct meizu_panel_factory *factory = READ_ONCE(meizu_factory);
	unsigned long flags;

	if (!factory || factory->display != display) return;
	if (factory->link_created) sysfs_remove_link(kobj, "panel");
	if (factory->group_created) sysfs_remove_group(kobj, &meizu_factory_group);
	/* Prevent new capture/connector references; the spinlock drains PPS. */
	spin_lock_irqsave(&meizu_factory_registry_lock, flags);
	WRITE_ONCE(meizu_factory, NULL);
	spin_unlock_irqrestore(&meizu_factory_registry_lock, flags);
	kref_put(&factory->references, meizu_factory_reference_release);
	wait_for_completion(&factory->drained);
	atomic_set(&meizu_dsi_log_mask, 0);
	kfree(factory);
}
