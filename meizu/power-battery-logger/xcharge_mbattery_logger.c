// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 stock recovery: export.text+0x1ac, probe+0x26c, sample+0x3b4,
 * alarm callback+0xa58. Exact tables from .data/.rodata relocations and BTF.
 * Exported enum uses implicit values without a trailing comma; its natural
 * genksyms CRC is stock8acce984. Keep that original token spelling.
 * This module only reads power supplies and logs; it sets no properties.
 * Plug transitions rearm a5s BOOTTIME alarm; sampling recurs after10000
 * jiffies(40s at stockHZ250) plus a30s wake alarm if either supply present.
 * Probe first samples after15000jiffies(60s). Thermal values in logs divide
 * propertyTEMP by10. All other numbers retain original property units.
 * Teardown now cancels alarms and work synchronously before destroying the
 * queue; stock destroyed the queue without cancelling its rearming timers.
 */
#include <linux/alarmtimer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include "../power.h"
#include "xcharge_mbattery_logger.h"

struct logger_element {
	char *psy_name;
	enum power_supply_property prop;
	int prop_val;
};
#include "tables.h"

static DEFINE_MUTEX(logger_lock);
static bool plug_in[MBATTERY_PLUG_TYPE_MAX];
static struct alarm logger_alarm;
static struct delayed_work logger_work;
static struct workqueue_struct *logger_wq;
static bool ready;
static bool stopping;

static void update_props(struct logger_element *table, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		union power_supply_propval value = { 0 };
		struct power_supply *psy = power_supply_get_by_name(table[i].psy_name);

		if (!psy) {
			pr_err("mbattery_logger: Failed to get %s psy\n", table[i].psy_name);
			mpower_print("Failed to get %s psy\n", table[i].psy_name);
			continue; /* Stock preserves the previous sample if no provider. */
		}
		power_supply_get_property(psy, table[i].prop, &value);
		table[i].prop_val = value.intval;
		power_supply_put(psy);
	}
}

static const char *lookup(const char * const *table, size_t count, int value)
{
	return table[(unsigned int)value < count ? value : 0];
}

#define FG(i) fuel_gage_list[i].prop_val
#define USB(i) wired_charging_list[i].prop_val
#define WLS(i) wireless_charging_list[i].prop_val
#define FG_FORMAT "[mpower][fuelgauge] <ui_soc=%d><status=%s><health=%s><ibat=%d><vbat=%d><tbat=%d><remaining_cap=%d><cycle=%d><TTF=%d><TTE=%d><lpd_en=%d><lpd_sta=%d>\n"
#define FG_ARGS FG(0), lookup(battery_status_text, ARRAY_SIZE(battery_status_text), FG(1)), lookup(battery_health_text, ARRAY_SIZE(battery_health_text), FG(2)), FG(3), FG(4), FG(5) / 10, FG(6), FG(7), FG(8), FG(9), FG(10), FG(11)
#define USB_FORMAT "[mpower][chg_usb] <usb_online=%d><real_type=%s><ibus=%d><vbus=%d><usb_temp=%d><usb_icl=%d><wired_level=%d><thermal_fcc=%d>\n"
#define USB_ARGS USB(0), lookup(adapter_text, ARRAY_SIZE(adapter_text), USB(5)), USB(1), USB(2), USB(3) / 10, USB(4), USB(6), USB(7)
#define WLS_FORMAT "[mpower][chg_wls] <wls_online=%d><wls_type=%s><wls_volt=%d><wls_curr=%d><thermal_fcc=%d>\n"
#define WLS_ARGS WLS(0), lookup(wls_text, ARRAY_SIZE(wls_text), WLS(1)), WLS(2), WLS(3), WLS(4)

static void logger_sample(struct work_struct *work)
{
	if (READ_ONCE(stopping))
		return;
	update_props(fuel_gage_list, ARRAY_SIZE(fuel_gage_list));
	pr_warn(FG_FORMAT, FG_ARGS);
	mpower_print(FG_FORMAT, FG_ARGS);
	update_props(wired_charging_list, ARRAY_SIZE(wired_charging_list));
	if (USB(0)) {
		pr_warn(USB_FORMAT, USB_ARGS);
		mpower_print(USB_FORMAT, USB_ARGS);
	}
	update_props(wireless_charging_list, ARRAY_SIZE(wireless_charging_list));
	if (WLS(0)) {
		pr_warn(WLS_FORMAT, WLS_ARGS);
		mpower_print(WLS_FORMAT, WLS_ARGS);
	}
	if (!READ_ONCE(stopping)) {
		queue_delayed_work(logger_wq, &logger_work, msecs_to_jiffies(40000));
		if (USB(0) || WLS(0))
			alarm_start_relative(&logger_alarm, ktime_set(30, 0));
	}
}

static enum alarmtimer_restart logger_alarm_cb(struct alarm *alarm, ktime_t now)
{
	if (!READ_ONCE(stopping))
		mod_delayed_work(logger_wq, &logger_work, 0);
	return ALARMTIMER_NORESTART;
}

void xcharge_mbattery_logger_update_plug_in_status(enum mbattery_plug_in_type type,
		int status)
{
	bool state = !!status;

	if ((unsigned int)type >= MBATTERY_PLUG_TYPE_MAX)
		return;
	mutex_lock(&logger_lock);
	if (plug_in[type] != state) {
		plug_in[type] = state;
		pr_warn("[mpower][charger] usb_in = %d, wls_in = %d\n",
				plug_in[USB_IN], plug_in[WLS_IN]);
		mpower_print("[mpower][charger] usb_in = %d, wls_in = %d\n",
				plug_in[USB_IN], plug_in[WLS_IN]);
		if (ready) {
			alarm_cancel(&logger_alarm);
			alarm_start_relative(&logger_alarm, ktime_set(5, 0));
		}
	}
	mutex_unlock(&logger_lock);
}
EXPORT_SYMBOL(xcharge_mbattery_logger_update_plug_in_status);

static int xcharge_mbattery_logger_probe(struct platform_device *pdev)
{
	mutex_lock(&logger_lock);
	if (ready) {
		mutex_unlock(&logger_lock);
		return -EBUSY;
	}
	logger_wq = create_singlethread_workqueue("mbattery_logger_wq");
	if (!logger_wq) {
		mutex_unlock(&logger_lock);
		return -ENOMEM;
	}
	WRITE_ONCE(stopping, false);
	INIT_DELAYED_WORK(&logger_work, logger_sample);
	alarm_init(&logger_alarm, ALARM_BOOTTIME, logger_alarm_cb);
	ready = true;
	queue_delayed_work(logger_wq, &logger_work, msecs_to_jiffies(60000));
	mutex_unlock(&logger_lock);
	return 0;
}

static int xcharge_mbattery_logger_remove(struct platform_device *pdev)
{
	mutex_lock(&logger_lock);
	ready = false;
	WRITE_ONCE(stopping, true);
	alarm_cancel(&logger_alarm);
	cancel_delayed_work_sync(&logger_work);
	/* Catch a work callback that passed its stopping check before removal. */
	alarm_cancel(&logger_alarm);
	destroy_workqueue(logger_wq);
	logger_wq = NULL;
	mutex_unlock(&logger_lock);
	return 0;
}

static const struct of_device_id logger_match[] = {
	{ .compatible = "xcharge,mbattery_logger" },
	{ }
};
MODULE_DEVICE_TABLE(of, logger_match);
static struct platform_driver logger_driver = {
	.probe = xcharge_mbattery_logger_probe,
	.remove = xcharge_mbattery_logger_remove,
	.driver = { .name = "xcharge_mbattery_logger", .of_match_table = logger_match },
};
module_platform_driver(logger_driver);
MODULE_LICENSE("GPL v2");
