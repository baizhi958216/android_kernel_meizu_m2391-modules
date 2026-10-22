// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reconstructed M2391 charger sysfs interface. Exact attribute names, modes,
 * directions and supply/property pairs are in attributes.h, recovered from
 * original .data relocations and callbacks .text+0x84c..0x1dd0.
 * Battery private properties305..309; wireless285..297; USB orientation285.
 * Levels are forwarded unchanged: zero asserts the respective discharge
 * voter, positive values clear an asserted voter, negative values preserve
 * that latch. No undocumented clamping, unit conversion or register values.
 * Reverse status1/2 maps to1, status6 to2, everything else0; PPS means USB
 * type8. Cooling max/current/set directly use battery307/308 with no scaling.
 * Unlike stock, this implementation releases supply/kobject references,
 * serializes voting/registration, handles sysfs setup errors and fully tears
 * down child directories before their parent.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include "../power.h"
#include "../system/meizu_sys.h"
#include "xcharge_chg_interface.h"

static DEFINE_MUTEX(interface_lock);
static DEFINE_MUTEX(vote_lock);
static DEFINE_MUTEX(level_lock);
static struct device *gdev;
static struct thermal_cooling_device *cooling;
static struct kobject *rev_kobj, *coil_kobj;
static unsigned long max_fcc;
static bool discharge_votes[DISCHARGING_VOTER_MAX];
static bool wired_discharging, wireless_discharging;

static int supply_get(const char *name, int property, int *value)
{
	struct power_supply *psy = power_supply_get_by_name(name);
	union power_supply_propval val = { 0 };
	int ret;

	if (!psy)
		return -ENODEV;
	ret = power_supply_get_property(psy,
			(enum power_supply_property)property, &val);
	*value = val.intval;
	power_supply_put(psy);
	return ret;
}
static int supply_set(const char *name, int property, int value)
{
	struct power_supply *psy = power_supply_get_by_name(name);
	union power_supply_propval val = { .intval = value };
	int ret;

	if (!psy)
		return -ENODEV;
	ret = power_supply_set_property(psy,
			(enum power_supply_property)property, &val);
	power_supply_put(psy);
	return ret;
}

void mz_chg_discharging_vote(enum discharging_voter voter, bool value)
{
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int i;

	if ((unsigned int)voter >= DISCHARGING_VOTER_MAX)
		return;
	mutex_lock(&vote_lock);
	psy = power_supply_get_by_name("battery");
	if (psy) {
		discharge_votes[voter] = value;
		for (i = 0; i < DISCHARGING_VOTER_MAX; i++)
			val.intval |= discharge_votes[i];
		power_supply_set_property(psy, (enum power_supply_property)305, &val);
		power_supply_put(psy);
	}
	mutex_unlock(&vote_lock);
}
EXPORT_SYMBOL(mz_chg_discharging_vote);

struct forwarding_attr {
	struct kobj_attribute *attribute;
	const char *supply;
	int property;
	const char *format;
	int special;
	bool hex_input;
};
static ssize_t forward_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t forward_store(struct kobject *, struct kobj_attribute *,
		const char *, size_t);
#include "attributes.h"

static const struct forwarding_attr *lookup_attr(struct kobj_attribute *attr)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(forwarding_attrs); i++)
		if (forwarding_attrs[i].attribute == attr)
			return &forwarding_attrs[i];
	return NULL;
}

static ssize_t forward_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	const struct forwarding_attr *a = lookup_attr(attr);
	int value = 0, ret;

	if (!a)
		return -EINVAL;
	ret = supply_get(a->supply, a->property, &value);
	if (ret == -ENODEV) {
		if (a->property >= 291 && a->property <= 296)
			return scnprintf(buf, PAGE_SIZE, "0x%x\n", ret);
		return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	}
	/* Stock initialized its sample to zero and ignored get-property errors. */
	if (a->special == 4)
		value = value == 1 || value == 2 ? 1 : value == 6 ? 2 : 0;
	else if (a->special == 5)
		value = value == 8;
	return scnprintf(buf, PAGE_SIZE, a->format, value);
}

static ssize_t forward_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	const struct forwarding_attr *a = lookup_attr(attr);
	bool state, *latched;
	struct power_supply *psy;
	unsigned int hex;
	int value, ret;

	if (!a)
		return -EINVAL;
	if (a->special == 3) {
		ret = kstrtobool(buf, &state);
		if (ret)
			return ret;
		/* Match the stock missing-provider error before casting the vote. */
		psy = power_supply_get_by_name("battery");
		if (!psy)
			return -ENODEV;
		power_supply_put(psy);
		mz_chg_discharging_vote(CMD_DISCHARGING, state);
		return count;
	}
	if (a->hex_input) {
		if (sscanf(buf, "0x%x", &hex) != 1)
			return -EINVAL;
		value = (int)hex;
	} else if (sscanf(buf, "%d", &value) != 1) {
		return -EINVAL;
	}
	mutex_lock(&level_lock);
	ret = supply_set(a->supply, a->property, value);
	if (ret == -ENODEV) {
		mutex_unlock(&level_lock);
		return ret;
	}
	if (a->special == 1 || a->special == 2) {
		latched = a->special == 1 ? &wired_discharging : &wireless_discharging;
		if ((!value && !*latched) || (value > 0 && *latched)) {
			state = !value;
			mz_chg_discharging_vote(a->special == 1 ? USER_CHARGE_LIMIT :
					USER_WLS_CHARGE_LIMIT, state);
			*latched = state;
		}
	}
	mutex_unlock(&level_lock);
	/* Stock returns byte count after a provider's set callback, even on error. */
	return count;
}

static struct mz_sysfs_directory charger_dir = { .dirname = "charger" };
static struct mz_sysfs_directory usb_dir = {
	.dirname = "usb_c", .attrs = usb_c_attrs, .attrs_size = ARRAY_SIZE(usb_c_attrs),
};
static struct mz_sysfs_directory wired_dir = {
	.dirname = "wired", .attrs = wired_attrs, .attrs_size = ARRAY_SIZE(wired_attrs),
};
static struct mz_sysfs_directory battery_dir = {
	.dirname = "battery", .attrs = battery_attrs, .attrs_size = ARRAY_SIZE(battery_attrs),
};
static struct mz_sysfs_directory wireless_dir = {
	.dirname = "wireless", .attrs = wireless_attrs, .attrs_size = ARRAY_SIZE(wireless_attrs),
};

static int cooling_get_max(struct thermal_cooling_device *cd, unsigned long *state)
{
	int value = 0, ret = supply_get("battery", 307, &value);

	if (ret)
		return ret;
	WRITE_ONCE(max_fcc, (unsigned long)(long)value);
	*state = READ_ONCE(max_fcc);
	return 0;
}
static int cooling_get_state(struct thermal_cooling_device *cd, unsigned long *state)
{
	int value = 0, ret = supply_get("battery", 308, &value);

	if (ret)
		return ret;
	/* Initialize the output even when stock would leave it unchanged. */
	*state = value && READ_ONCE(max_fcc) ? (unsigned long)(long)value : 0;
	return 0;
}
static int cooling_set_state(struct thermal_cooling_device *cd, unsigned long state)
{
	if (!READ_ONCE(max_fcc))
		return 0;
	return supply_set("battery", 308, (int)state);
}
static const struct thermal_cooling_device_ops cooling_ops = {
	.get_max_state = cooling_get_max,
	.get_cur_state = cooling_get_state,
	.set_cur_state = cooling_set_state,
};

void xcharge_chg_interface_register_cd(void)
{
	mutex_lock(&interface_lock);
	if (gdev && !cooling) {
		cooling = thermal_of_cooling_device_register(gdev->of_node,
				"xcharge_chg", NULL, &cooling_ops);
		if (IS_ERR(cooling))
			cooling = NULL;
	}
	mutex_unlock(&interface_lock);
}
EXPORT_SYMBOL(xcharge_chg_interface_register_cd);

static void notify_kobject(struct kobject **slot, char **env)
{
	struct kobject *kobj;

	mutex_lock(&interface_lock);
	kobj = kobject_get(*slot);
	mutex_unlock(&interface_lock);
	if (kobj) {
		kobject_uevent_env(kobj, KOBJ_CHANGE, env);
		kobject_put(kobj);
	}
}
void xcharge_chg_interface_notify_rev_chg_sta(char *env_str[])
{
	notify_kobject(&rev_kobj, env_str);
}
EXPORT_SYMBOL(xcharge_chg_interface_notify_rev_chg_sta);
void xcharge_chg_interface_notify_coil_pwr_on_sta(char *env_str[])
{
	notify_kobject(&coil_kobj, env_str);
}
EXPORT_SYMBOL(xcharge_chg_interface_notify_coil_pwr_on_sta);

static void destroy_dirs(void)
{
	if (wireless_dir.dir)
		mz_sysfs_destory_directory(&wireless_dir);
	if (battery_dir.dir)
		mz_sysfs_destory_directory(&battery_dir);
	if (wired_dir.dir)
		mz_sysfs_destory_directory(&wired_dir);
	if (usb_dir.dir)
		mz_sysfs_destory_directory(&usb_dir);
	if (charger_dir.dir)
		mz_sysfs_destory_directory(&charger_dir);
}

static int interface_probe(struct platform_device *pdev)
{
	int ret;

	mutex_lock(&interface_lock);
	if (gdev) {
		mutex_unlock(&interface_lock);
		return -EBUSY;
	}
	ret = mz_sysfs_create_directory(&charger_dir, NULL);
	if (ret)
		goto fail;
	ret = mz_sysfs_create_directory(&usb_dir, NULL);
	if (ret)
		goto fail;
	ret = mz_sysfs_create_directory_specify_parent(&wired_dir, NULL, charger_dir.dir);
	if (ret)
		goto fail;
	ret = mz_sysfs_create_directory_specify_parent(&battery_dir, NULL, charger_dir.dir);
	if (ret)
		goto fail;
	ret = mz_sysfs_create_directory_specify_parent(&wireless_dir, NULL, charger_dir.dir);
	if (ret)
		goto fail;
	rev_kobj = kobject_create_and_add("rev_chg", wireless_dir.dir);
	coil_kobj = kobject_create_and_add("coil_pwr_on", wireless_dir.dir);
	if (!rev_kobj || !coil_kobj) {
		ret = -ENOMEM;
		goto fail;
	}
	gdev = &pdev->dev;
	mutex_unlock(&interface_lock);
	return 0;
fail:
	kobject_put(rev_kobj);
	kobject_put(coil_kobj);
	rev_kobj = coil_kobj = NULL;
	destroy_dirs();
	mutex_unlock(&interface_lock);
	return ret;
}

static int interface_remove(struct platform_device *pdev)
{
	struct kobject *rev, *coil;

	mutex_lock(&interface_lock);
	gdev = NULL;
	if (cooling)
		thermal_cooling_device_unregister(cooling);
	cooling = NULL;
	rev = rev_kobj;
	coil = coil_kobj;
	rev_kobj = coil_kobj = NULL;
	kobject_put(rev);
	kobject_put(coil);
	destroy_dirs();
	mutex_unlock(&interface_lock);
	return 0;
}
static const struct of_device_id interface_match[] = {
	{ .compatible = "xcharge,xcharge_chg_interface" },
	{ }
};
MODULE_DEVICE_TABLE(of, interface_match);
static struct platform_driver interface_driver = {
	.probe = interface_probe,
	.remove = interface_remove,
	.driver = { .name = "xcharge_chg_interface", .of_match_table = interface_match },
};
module_platform_driver(interface_driver);
MODULE_LICENSE("GPL v2");
