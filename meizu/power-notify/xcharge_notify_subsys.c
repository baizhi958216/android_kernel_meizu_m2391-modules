// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 power plug notification, reconstructed from its original module.
 *
 * Evidence (ELF .text offsets, original 5.15.119 module):
 *   0x1ac setter: accepts types 0/1, ignores identical status, queues zero-delay
 *         work only after probe; exports no synchronous initial notification.
 *   0x424 worker: wireless takes priority, suppresses unchanged effective type;
 *         events are USB in/out=0/1 and wireless in/out=2/3, data=NULL.
 *   0x304 probe: allocates a single-thread ordered reclaim-capable workqueue.
 *
 * This source adds serialization and drains delayed work during removal. The
 * exported setter's enum tag/values are recovered from split BTF; its full
 * prototype reproduces stock genksyms CRC 0x98fe0097 without CRC overrides.
 * Remaining imported/exported ABI and runtime checks still precede replacement.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "xcharge_notify_subsys.h"

static BLOCKING_NOTIFIER_HEAD(mpower_notifier_head);
static DEFINE_SPINLOCK(plug_lock);
static DEFINE_MUTEX(queue_lifecycle_lock);
static struct workqueue_struct *notify_wq;
static struct delayed_work plug_work;
static bool plug_status[2];

/* 0=none, 1=USB, 2=wireless. Original .bss starts with no notification. */
static unsigned int last_type;

int register_mpower_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mpower_notifier_head, nb);
}
EXPORT_SYMBOL(register_mpower_notifier);

int unregister_mpower_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mpower_notifier_head, nb);
}
EXPORT_SYMBOL(unregister_mpower_notifier);

static void plug_work_fn(struct work_struct *work)
{
	unsigned long flags, event;
	unsigned int type;

	spin_lock_irqsave(&plug_lock, flags);
	type = plug_status[WLS_PLUG] ? 2 :
		plug_status[USB_PLUG] ? 1 : 0;
	spin_unlock_irqrestore(&plug_lock, flags);

	if (type == last_type)
		return;
	if (type == 2)
		event = WLS_CHARGING;
	else if (type == 1)
		event = WIRE_CHARGING;
	else if (last_type == 1)
		event = WIRE_NCHARGING;
	else
		event = WLS_NCHARGING;

	blocking_notifier_call_chain(&mpower_notifier_head, event, NULL);
	last_type = type;
}

void xcharge_notify_subsys_set_plug_status(
		enum xcharge_notify_subsys_plug_type type, bool plugged)
{
	unsigned long flags;

	if ((unsigned int)type >= PLUG_TYPE_MAX)
		return;
	spin_lock_irqsave(&plug_lock, flags);
	if (notify_wq && plug_status[type] != plugged) {
		plug_status[type] = plugged;
		/* Zero delay, with the last observed pair of USB/wireless states. */
		mod_delayed_work(notify_wq, &plug_work, 0);
	}
	spin_unlock_irqrestore(&plug_lock, flags);
}
EXPORT_SYMBOL(xcharge_notify_subsys_set_plug_status);

static int xcharge_notify_probe(struct platform_device *pdev)
{
	struct workqueue_struct *wq;
	unsigned long flags;

	mutex_lock(&queue_lifecycle_lock);
	wq = alloc_ordered_workqueue("xcharge_notify_subsys_wq", WQ_MEM_RECLAIM);
	if (!wq) {
		mutex_unlock(&queue_lifecycle_lock);
		return -ENOMEM;
	}
	spin_lock_irqsave(&plug_lock, flags);
	if (notify_wq) {
		spin_unlock_irqrestore(&plug_lock, flags);
		destroy_workqueue(wq);
		mutex_unlock(&queue_lifecycle_lock);
		return -EBUSY;
	}
	INIT_DELAYED_WORK(&plug_work, plug_work_fn);
	plug_status[0] = false;
	plug_status[1] = false;
	last_type = 0;
	notify_wq = wq;
	spin_unlock_irqrestore(&plug_lock, flags);
	mutex_unlock(&queue_lifecycle_lock);
	return 0;
}

static int xcharge_notify_remove(struct platform_device *pdev)
{
	struct workqueue_struct *wq;
	unsigned long flags;

	mutex_lock(&queue_lifecycle_lock);
	spin_lock_irqsave(&plug_lock, flags);
	wq = notify_wq;
	notify_wq = NULL;
	spin_unlock_irqrestore(&plug_lock, flags);
	cancel_delayed_work_sync(&plug_work);
	destroy_workqueue(wq);
	mutex_unlock(&queue_lifecycle_lock);
	return 0;
}

static const struct of_device_id xcharge_notify_match[] = {
	{ .compatible = "xcharge,notify_subsys" },
	{ }
};
MODULE_DEVICE_TABLE(of, xcharge_notify_match);

static struct platform_driver xcharge_notify_driver = {
	.probe = xcharge_notify_probe,
	.remove = xcharge_notify_remove,
	.driver = {
		.name = "xcharge_notify_subsys",
		.of_match_table = xcharge_notify_match,
	},
};
module_platform_driver(xcharge_notify_driver);

MODULE_DESCRIPTION("Meizu M2391 power plug notification");
MODULE_LICENSE("GPL v2");
