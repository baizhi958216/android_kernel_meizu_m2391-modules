// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/soc/qcom/panel_event_notifier.h>

#include "cs_m68.h"

static void cs_m68_update_work(struct work_struct *work)
{
	struct cs_m68 *cs = container_of(to_delayed_work(work),
		struct cs_m68, update_work);
	int ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed)
		goto out;
	ret = cs_m68_update_firmware(cs);
	if (!ret)
		cs_m68_restore_calibration(cs);
	cs_m68_set_irq(cs, true);
	WRITE_ONCE(cs->initialized, true);
out:
	mutex_unlock(&cs->access_lock);
}

static void cs_m68_health_work(struct work_struct *work)
{
	struct cs_m68 *cs = container_of(to_delayed_work(work),
		struct cs_m68, health_work);
	int ret;

	mutex_lock(&cs->access_lock);
	if (!cs->removed && cs->initialized) {
		ret = cs_m68_check_health(cs);
		if (!ret)
			queue_delayed_work(cs->workqueue, &cs->health_work, 750);
	}
	mutex_unlock(&cs->access_lock);
}

static void cs_m68_blank_work(struct work_struct *work)
{
	struct cs_m68 *cs = container_of(work, struct cs_m68, blank_work);

	mutex_lock(&cs->access_lock);
	if (!cs->removed) {
		cs_m68_release_key(cs);
		gpio_set_value(cs->enable_gpio, 0);
		msleep(10);
		cs_m68_set_irq(cs, false);
	}
	mutex_unlock(&cs->access_lock);
}

static void cs_m68_unblank_work(struct work_struct *work)
{
	struct cs_m68 *cs = container_of(work, struct cs_m68, unblank_work);
	u8 identity;

	mutex_lock(&cs->access_lock);
	if (!cs->removed) {
		cs_m68_set_irq(cs, false);
		gpio_set_value(cs->enable_gpio, 1);
		msleep(10);
		mutex_lock(&cs->command_lock);
		cs_m68_read(cs, 0x00, &identity, 1);
		cs_m68_update_skip_command(cs);
		mutex_unlock(&cs->command_lock);
		msleep(10);
		cs_m68_set_irq(cs, true);
	}
	mutex_unlock(&cs->access_lock);
}

static void cs_m68_panel_event(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *data)
{
	struct cs_m68 *cs = data;

	if (!notification || !READ_ONCE(cs->initialized) || READ_ONCE(cs->removed))
		return;
	if (notification->notif_type == DRM_PANEL_EVENT_BLANK ||
	    notification->notif_type == DRM_PANEL_EVENT_BLANK_LP)
		queue_work(cs->panel_workqueue, &cs->blank_work);
	else if (notification->notif_type == DRM_PANEL_EVENT_UNBLANK)
		queue_work(cs->panel_workqueue, &cs->unblank_work);
}

static void cs_m68_register_panel(struct work_struct *work)
{
	struct cs_m68 *cs = container_of(to_delayed_work(work),
		struct cs_m68, panel_register_work);
	struct device_node *node;
	struct drm_panel *panel;
	void *cookie;
	int count, i;

	mutex_lock(&cs->access_lock);
	if (cs->removed)
		goto out;
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
			cs_m68_panel_event, cs);
		if (!IS_ERR_OR_NULL(cookie))
			cs->panel_cookie = cookie;
		break;
	}
out:
	mutex_unlock(&cs->access_lock);
}

int cs_m68_start_runtime(struct cs_m68 *cs)
{
	INIT_DELAYED_WORK(&cs->update_work, cs_m68_update_work);
	INIT_DELAYED_WORK(&cs->panel_register_work, cs_m68_register_panel);
	INIT_DELAYED_WORK(&cs->health_work, cs_m68_health_work);
	INIT_WORK(&cs->blank_work, cs_m68_blank_work);
	INIT_WORK(&cs->unblank_work, cs_m68_unblank_work);
	cs->workqueue = alloc_ordered_workqueue("cs_press", WQ_MEM_RECLAIM);
	if (!cs->workqueue)
		return -ENOMEM;
	cs->panel_workqueue = alloc_ordered_workqueue("cs_press_notify", WQ_MEM_RECLAIM);
	if (!cs->panel_workqueue) {
		destroy_workqueue(cs->workqueue);
		cs->workqueue = NULL;
		return -ENOMEM;
	}
	queue_delayed_work(cs->workqueue, &cs->update_work, 200);
	queue_delayed_work(cs->workqueue, &cs->panel_register_work, 500);
	return 0;
}

/* Set removed under access_lock first, then call without holding that lock. */
void cs_m68_stop_runtime(struct cs_m68 *cs)
{
	if (!cs->workqueue)
		return;
	cancel_delayed_work_sync(&cs->panel_register_work);
	if (cs->panel_cookie) {
		panel_event_notifier_unregister(cs->panel_cookie);
		cs->panel_cookie = NULL;
	}
	cancel_delayed_work_sync(&cs->update_work);
	cancel_delayed_work_sync(&cs->health_work);
	cancel_work_sync(&cs->blank_work);
	cancel_work_sync(&cs->unblank_work);
	destroy_workqueue(cs->panel_workqueue);
	destroy_workqueue(cs->workqueue);
	cs->panel_workqueue = NULL;
	cs->workqueue = NULL;
}
