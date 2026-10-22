// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of update_work/panel notification paths. */
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/soc/qcom/panel_event_notifier.h>

#include "cs_f71.h"

static void cs_f71_boot_work(struct work_struct *work)
{
	struct cs_f71 *cs = container_of(to_delayed_work(work), struct cs_f71, boot_work);
	u8 info[4];
	unsigned int attempt;
	bool identified = false;

	mutex_lock(&cs->operation_lock);
	cs_f71_irq_block(cs, CS_F71_IRQ_FIRMWARE);
	mutex_lock(&cs->command_lock);
	if (!cs->removed) {
		for (attempt = 0; attempt < 3; attempt++) {
			if (!cs_f71_boot_info_locked(cs, info) && info[0] == 0x71) {
				identified = true;
				break;
			}
		}
	}
	mutex_unlock(&cs->command_lock);
	cs_f71_irq_unblock(cs, CS_F71_IRQ_FIRMWARE);
	mutex_unlock(&cs->operation_lock);
	if (identified)
		cs_f71_queue_firmware(cs, false);
}

void cs_f71_apply_panel_state(struct cs_f71 *cs)
{
	int desired;

	lockdep_assert_held(&cs->operation_lock);
	if (READ_ONCE(cs->removed))
		return;
	desired = READ_ONCE(cs->panel_desired);
	if (!desired)
		return;
	cs_f71_irq_block(cs, CS_F71_IRQ_PANEL);
	mutex_lock(&cs->command_lock);
	if (!cs->removed) {
		if (desired == 2)
			cs_f71_release_keys_locked(cs);
		gpiod_set_raw_value_cansleep(cs->enable_gpio, desired == 1);
		msleep(10);
	}
	mutex_unlock(&cs->command_lock);
	if (desired == 1)
		cs_f71_irq_unblock(cs, CS_F71_IRQ_PANEL | CS_F71_IRQ_SETUP);
}

static void cs_f71_panel_work(struct work_struct *work)
{
	struct cs_f71 *cs = container_of(work, struct cs_f71, panel_work);

	mutex_lock(&cs->operation_lock);
	cs_f71_apply_panel_state(cs);
	mutex_unlock(&cs->operation_lock);
}

static void cs_f71_panel_event(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *data)
{
	struct cs_f71 *cs = data;

	if (!notification || READ_ONCE(cs->removed))
		return;
	if (notification->notif_type == DRM_PANEL_EVENT_BLANK ||
	    notification->notif_type == DRM_PANEL_EVENT_BLANK_LP)
		WRITE_ONCE(cs->panel_desired, 2);
	else if (notification->notif_type == DRM_PANEL_EVENT_UNBLANK)
		WRITE_ONCE(cs->panel_desired, 1);
	else
		return;
	/* Work takes operation_lock, so programming cannot be power-cut. One
	 * state worker prevents coalesced blank/unblank work from reordering.
	 */
	queue_work(cs->panel_queue, &cs->panel_work);
}

static void cs_f71_register_panel(struct work_struct *work)
{
	struct cs_f71 *cs = container_of(to_delayed_work(work),
			struct cs_f71, panel_register_work);
	struct device_node *node;
	struct drm_panel *panel;
	void *cookie;
	int count, i;

	if (READ_ONCE(cs->removed))
		return;
	count = of_count_phandle_with_args(cs->client->dev.of_node, "panel", NULL);
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(cs->client->dev.of_node, "panel", i);
		if (!node)
			continue;
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (IS_ERR_OR_NULL(panel))
			continue;
		cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
				PANEL_EVENT_NOTIFIER_CLIENT_FORCE_TOUCH, panel,
				cs_f71_panel_event, cs);
		if (!IS_ERR_OR_NULL(cookie))
			cs->panel_cookie = cookie;
		break;
	}
}

int cs_f71_runtime_init(struct cs_f71 *cs)
{
	INIT_DELAYED_WORK(&cs->boot_work, cs_f71_boot_work);
	INIT_DELAYED_WORK(&cs->panel_register_work, cs_f71_register_panel);
	INIT_WORK(&cs->panel_work, cs_f71_panel_work);
	cs->panel_queue = alloc_ordered_workqueue("cs_press_notify", WQ_MEM_RECLAIM);
	return cs->panel_queue ? 0 : -ENOMEM;
}

void cs_f71_runtime_start(struct cs_f71 *cs)
{
	/* Preserve original jiffy delays rather than assuming milliseconds. */
	queue_delayed_work(system_wq, &cs->boot_work, 1000);
	queue_delayed_work(cs->panel_queue, &cs->panel_register_work, 500);
}

void cs_f71_runtime_exit(struct cs_f71 *cs)
{
	if (!cs->panel_queue)
		return;
	cancel_delayed_work_sync(&cs->panel_register_work);
	if (cs->panel_cookie) {
		panel_event_notifier_unregister(cs->panel_cookie);
		cs->panel_cookie = NULL;
	}
	cancel_delayed_work_sync(&cs->boot_work);
	cancel_work_sync(&cs->panel_work);
	destroy_workqueue(cs->panel_queue);
	cs->panel_queue = NULL;
}
