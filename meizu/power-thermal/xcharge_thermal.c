// SPDX-License-Identifier: GPL-2.0-only
/*
 * Recovered from M2391 stock xcharge_thermal.ko. BTF gives unsigned int
 * arguments; original CRCs tbat64bf93c3/tusbe22aeac7 reproduce naturally.
 * Data starts at2500; get_temp multiplies by10. USB work compares signed
 * samples to6000/4500, checks USB ONLINE(property4), and only updates its
 * thermal zone on hysteresis transitions. There are no hardware writes.
 * Startup queues the USB work after15000 jiffies(60seconds at stockHZ250),
 * whose first execution enables subsequent external temperature updates.
 * Three sensor IDs0,1,2 expose USB,battery-high,battery-low temperatures.
 * Lifetime bugs in stock(remove only cancels one work item) are repaired:
 * stop publishing first, synchronously drain both works, destroy the queue.
 */
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include "xcharge_thermal.h"

struct charger_thermal_dev {
	struct workqueue_struct *wq;
	struct delayed_work usb_work;
	struct delayed_work battery_work;
	struct thermal_zone_device *usb;
	struct thermal_zone_device *battery_high;
	struct thermal_zone_device *battery_low;
	bool enabled;
	bool usb_hot;
};
static DEFINE_SPINLOCK(thermal_lock);
static struct charger_thermal_dev *thermal_dev;
static unsigned int tbat = 2500;
static unsigned int tusb = 2500;

static bool usable(struct thermal_zone_device *tzd)
{
	return !IS_ERR_OR_NULL(tzd);
}

void xcharge_thermal_update_tusb(unsigned int value)
{
	struct charger_thermal_dev *d;
	unsigned long flags;

	spin_lock_irqsave(&thermal_lock, flags);
	d = thermal_dev;
	if (d && READ_ONCE(d->enabled) && usable(d->usb) && value != tusb) {
		WRITE_ONCE(tusb, value);
		queue_delayed_work(d->wq, &d->usb_work, 0);
	}
	spin_unlock_irqrestore(&thermal_lock, flags);
}
EXPORT_SYMBOL(xcharge_thermal_update_tusb);

void xcharge_thermal_update_tbat(unsigned int value)
{
	struct charger_thermal_dev *d;
	unsigned long flags;

	spin_lock_irqsave(&thermal_lock, flags);
	d = thermal_dev;
	if (d && READ_ONCE(d->enabled) && usable(d->battery_high) &&
			usable(d->battery_low) && value != tbat) {
		WRITE_ONCE(tbat, value);
		queue_delayed_work(d->wq, &d->battery_work, 0);
	}
	spin_unlock_irqrestore(&thermal_lock, flags);
}
EXPORT_SYMBOL(xcharge_thermal_update_tbat);

static void usb_thermal_cb(struct work_struct *work)
{
	struct charger_thermal_dev *d = container_of(to_delayed_work(work),
			struct charger_thermal_dev, usb_work);
	struct power_supply *psy;
	union power_supply_propval online = { 0 };
	int temperature;
	bool hot;

	if (!READ_ONCE(d->enabled)) {
		WRITE_ONCE(d->enabled, true);
		return;
	}
	psy = power_supply_get_by_name("usb");
	if (!psy)
		return;
	power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &online);
	power_supply_put(psy);
	temperature = (int)READ_ONCE(tusb);
	hot = d->usb_hot;
	if (temperature >= 6000 && !hot && online.intval)
		hot = true;
	else if (temperature <= 4500 && hot)
		hot = false;
	if (hot != d->usb_hot) {
		d->usb_hot = hot;
		if (usable(d->usb))
			thermal_zone_device_update(d->usb, THERMAL_EVENT_UNSPECIFIED);
	}
}

static void battery_thermal_cb(struct work_struct *work)
{
	struct charger_thermal_dev *d = container_of(to_delayed_work(work),
			struct charger_thermal_dev, battery_work);

	if (!READ_ONCE(d->enabled))
		return;
	if (usable(d->battery_high))
		thermal_zone_device_update(d->battery_high, THERMAL_EVENT_UNSPECIFIED);
	if (usable(d->battery_low))
		thermal_zone_device_update(d->battery_low, THERMAL_EVENT_UNSPECIFIED);
}

static int usb_thermal_get_temp(void *data, int *temp)
{
	*temp = (int)(READ_ONCE(tusb) * 10U);
	return 0;
}
static int battery_thermal_get_temp(void *data, int *temp)
{
	*temp = (int)(READ_ONCE(tbat) * 10U);
	return 0;
}
static const struct thermal_zone_of_device_ops usb_ops = {
	.get_temp = usb_thermal_get_temp,
};
static const struct thermal_zone_of_device_ops battery_ops = {
	.get_temp = battery_thermal_get_temp,
};

static int xcharge_thermal_probe(struct platform_device *pdev)
{
	struct charger_thermal_dev *d;
	unsigned long flags;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->wq = create_singlethread_workqueue("charger_thermal_workqueue");
	if (!d->wq)
		return -ENOMEM;
	INIT_DELAYED_WORK(&d->usb_work, usb_thermal_cb);
	INIT_DELAYED_WORK(&d->battery_work, battery_thermal_cb);
	d->usb = devm_thermal_zone_of_sensor_register(&pdev->dev, 0, pdev, &usb_ops);
	d->battery_high = devm_thermal_zone_of_sensor_register(&pdev->dev, 1,
			pdev, &battery_ops);
	d->battery_low = devm_thermal_zone_of_sensor_register(&pdev->dev, 2,
			pdev, &battery_ops);
	platform_set_drvdata(pdev, d);
	spin_lock_irqsave(&thermal_lock, flags);
	if (thermal_dev) {
		spin_unlock_irqrestore(&thermal_lock, flags);
		destroy_workqueue(d->wq);
		return -EBUSY;
	}
	thermal_dev = d;
	queue_delayed_work(d->wq, &d->usb_work, msecs_to_jiffies(60000));
	spin_unlock_irqrestore(&thermal_lock, flags);
	return 0;
}
static int xcharge_thermal_remove(struct platform_device *pdev)
{
	struct charger_thermal_dev *d = platform_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&thermal_lock, flags);
	thermal_dev = NULL;
	spin_unlock_irqrestore(&thermal_lock, flags);
	cancel_delayed_work_sync(&d->usb_work);
	cancel_delayed_work_sync(&d->battery_work);
	destroy_workqueue(d->wq);
	return 0;
}
static const struct of_device_id thermal_match[] = {
	{ .compatible = "xcharge,xcharge_thermal" },
	{ }
};
MODULE_DEVICE_TABLE(of, thermal_match);
static struct platform_driver thermal_driver = {
	.probe = xcharge_thermal_probe,
	.remove = xcharge_thermal_remove,
	.driver = { .name = "xcharge_thermal", .of_match_table = thermal_match },
};
module_platform_driver(thermal_driver);
MODULE_LICENSE("GPL v2");
