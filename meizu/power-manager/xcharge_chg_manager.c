// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 wireless charger manager reconstruction.
 * Stock .text: status request 0x14c, probe 0x1a4, status work 0x360,
 * firmware work 0x56c..0x8b0; enum IDs from its split BTF.
 *
 * Firmware protocol: property291 packs target version in high16 and running
 * version in low16. Private slot70 stores the full verified version word.
 * Request update289 with value0, verify290, reread291 and only then persist.
 * First check is 60s after probe, unsuccessful checks repeat every10s.
 *
 * Unlike stock, transport errors cannot reuse stale union values; acquired
 * power-supply references are released and removal drains both delayed works.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "../power.h"
#include "../storage/meizu_storage.h"

static DEFINE_SPINLOCK(manager_lock);
static DEFINE_MUTEX(manager_lifecycle_lock);
static struct workqueue_struct *manager_wq;
static struct delayed_work status_work, firmware_work;
static u32 stored_version;
static bool firmware_complete, force_upgrade;

static int wireless_get(struct power_supply *psy,
		enum mz_wireless_property_id property, int *value)
{
	union power_supply_propval val = { 0 };
	int ret;

	ret = power_supply_get_property(psy,
			(enum power_supply_property)property, &val);
	if (!ret)
		*value = val.intval;
	return ret;
}

void xcharge_wls_status_get_handle(void)
{
	unsigned long flags;

	spin_lock_irqsave(&manager_lock, flags);
	if (manager_wq)
		mod_delayed_work(manager_wq, &status_work, 0);
	spin_unlock_irqrestore(&manager_lock, flags);
}
EXPORT_SYMBOL(xcharge_wls_status_get_handle);

static void wireless_status_work(struct work_struct *work)
{
	struct power_supply *psy = power_supply_get_by_name("wireless");
	char *env[] = { NULL, NULL };
	int reverse, model, coil, power, active;

	if (!psy) {
		mpower_print("Failed to get wireless psy\n");
		return;
	}
	if (!wireless_get(psy, XCHARGE_WLS_REV_CHG_STAT, &reverse)) {
		mpower_print("[mpower][chg_wls] wls_chg_stat=%d\n", reverse);
		if (reverse == 1 || reverse == 6) {
			env[0] = reverse == 1 ? "reverse_status=1" : "reverse_status=2";
			xcharge_chg_interface_notify_rev_chg_sta(env);
		}
	}
	if (!wireless_get(psy, XCHARGE_WLS_TX_MODEL_ID, &model) &&
		!wireless_get(psy, XCHARGE_WLS_TX_COIL_ID, &coil) &&
		!wireless_get(psy, XCHARGE_WLS_TX_MAX_POWER, &power))
		mpower_print("[mpower][chg_wls] WLS_TX_INFO: MODULE=%04x, COIL=%d, POWER=%d\n",
				model, coil, power);
	if (!wireless_get(psy, XCHARGE_WLS_COIL_PWR_ON, &active)) {
		mpower_print("[mpower][chg_wls] wls_coil_pwr_on_sta=%d\n", active);
		if (active == 1) {
			env[0] = "wls_coil_pwr_on";
			xcharge_chg_interface_notify_coil_pwr_on_sta(env);
			mpower_print("[mpower][chg_wls] WLS_COIL_PWR_ON\n");
		}
	}
	power_supply_put(psy);
}

static void wireless_firmware_work(struct work_struct *work)
{
	struct power_supply *psy = power_supply_get_by_name("wireless");
	union power_supply_propval request = { .intval = 0 };
	unsigned long flags;
	u32 version;
	int value, verified, ret;
	u16 target, running;

	if (!psy)
		goto retry;
	ret = wireless_get(psy, XCHARGE_WLS_FW_VERSION, &value);
	if (ret || !value)
		goto put_retry;
	version = value;
	if (version == stored_version) {
		firmware_complete = true;
		mpower_print("[mpower][chg_wls] wls latest version: version=0x%x\n", version);
		goto put_retry;
	}
	target = version >> 16;
	running = version & 0xffff;
	if (target != running || force_upgrade) {
		ret = power_supply_set_property(psy,
			(enum power_supply_property)XCHARGE_WLS_FW_UPDATE, &request);
		if (ret)
			goto put_retry;
		ret = wireless_get(psy, XCHARGE_WLS_FW_VERSION, &value);
		/* Stock treats a nonzero word here as an upgrade failure. */
		if (ret || value)
			goto put_retry;
		mpower_print("[mpower][chg_wls] wireless charger firmware upgrade completed\n");
	}
	ret = wireless_get(psy, XCHARGE_WLS_FW_VERIFY, &verified);
	if (ret || verified) {
		if (!ret && target == running)
			force_upgrade = true;
		goto put_retry;
	}
	ret = wireless_get(psy, XCHARGE_WLS_FW_VERSION, &value);
	if (ret || !value)
		goto put_retry;
	version = value;
	ret = private_slot_rw(1, 70, &version, sizeof(version));
	/* Do not mark persisted completion if the backing partition write failed. */
	if (!ret) {
		stored_version = version;
		firmware_complete = true;
		mpower_print("[mpower][chg_wls] wireless charger firmware verify completed, version=0x%x\n",
				stored_version);
	}
put_retry:
	power_supply_put(psy);
retry:
	spin_lock_irqsave(&manager_lock, flags);
	if (manager_wq && !firmware_complete)
		queue_delayed_work(manager_wq, &firmware_work, 10 * HZ);
	spin_unlock_irqrestore(&manager_lock, flags);
}

static int xcharge_manager_probe(struct platform_device *pdev)
{
	struct workqueue_struct *wq;
	unsigned long flags;

	mutex_lock(&manager_lifecycle_lock);
	if (manager_wq) {
		mutex_unlock(&manager_lifecycle_lock);
		return -EBUSY;
	}
	wq = alloc_ordered_workqueue("xcharge_chg_manager_wq", WQ_MEM_RECLAIM);
	if (!wq) {
		mutex_unlock(&manager_lifecycle_lock);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&status_work, wireless_status_work);
	INIT_DELAYED_WORK(&firmware_work, wireless_firmware_work);
	stored_version = 0;
	firmware_complete = false;
	force_upgrade = false;
	private_slot_rw(0, 70, &stored_version, sizeof(stored_version));
	spin_lock_irqsave(&manager_lock, flags);
	manager_wq = wq;
	queue_delayed_work(wq, &firmware_work, 60 * HZ);
	spin_unlock_irqrestore(&manager_lock, flags);
	mutex_unlock(&manager_lifecycle_lock);
	return 0;
}

static int xcharge_manager_remove(struct platform_device *pdev)
{
	struct workqueue_struct *wq;
	unsigned long flags;

	mutex_lock(&manager_lifecycle_lock);
	spin_lock_irqsave(&manager_lock, flags);
	wq = manager_wq;
	manager_wq = NULL;
	spin_unlock_irqrestore(&manager_lock, flags);
	cancel_delayed_work_sync(&status_work);
	cancel_delayed_work_sync(&firmware_work);
	destroy_workqueue(wq);
	mutex_unlock(&manager_lifecycle_lock);
	return 0;
}

static const struct of_device_id xcharge_manager_match[] = {
	{ .compatible = "xcharge,xcharge_chg_manager" },
	{ }
};
MODULE_DEVICE_TABLE(of, xcharge_manager_match);
static struct platform_driver xcharge_manager_driver = {
	.probe = xcharge_manager_probe,
	.remove = xcharge_manager_remove,
	.driver = {
		.name = "xcharge_chg_manager",
		.of_match_table = xcharge_manager_match,
	},
};
module_platform_driver(xcharge_manager_driver);
MODULE_DESCRIPTION("Recovered M2391 wireless charging manager");
MODULE_LICENSE("GPL v2");
