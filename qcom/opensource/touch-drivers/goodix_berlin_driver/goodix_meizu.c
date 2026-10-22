// SPDX-License-Identifier: GPL-2.0
/* OEM command/sysfs behavior recovered from M2391 goodix_ts.ko:
 * .text 0x898 gesture, 0xbcc GIO4, 0x39e8 report, 0x52ec FOD command.
 * Normal/LP FOD counters, GIO4 recovery and charging commands/notifier recovered
 * from original IRQ/worker paths. Explicit opt-in pending complete module ABI
 * verification and hardware replacement validation.
 */
#include <linux/delay.h>
#include <linux/input/mt.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>
#include "goodix_ts_core.h"
#include "goodix_meizu.h"
#include "../mz_gesture/mz_gesture_ts.h"
#include "../../../../meizu/system/meizu_sys.h"
#include "../../../../meizu/power-notify/xcharge_notify_subsys.h"

#define MEIZU_COMMAND_REG 0x10174
struct goodix_meizu {
	struct mutex command_lock;
	struct mutex state_lock;
	struct goodix_ts_core *cd;
	struct delayed_work force_release;
	struct delayed_work reenable;
	bool normal_pressed;
	bool lp_pressed;
	bool fod_active;
	bool lp_origin;
	u8 report_frames;
	u8 single_frames;
	bool stopping;
	bool wake_enabled;
	bool group_created;
	bool linked;
	struct notifier_block power_notifier;
	struct work_struct charge_work;
	struct work_struct unplug_work;
	struct workqueue_struct *charge_wq;
	unsigned int charge_mode;
	bool power_registered;
};

bool goodix_meizu_is_ready(struct goodix_ts_core *cd)
{
	struct goodix_meizu *mz = smp_load_acquire(&cd->meizu);

	return mz && !READ_ONCE(mz->stopping);
}

static int meizu_write(struct goodix_ts_core *cd, const u8 *cmd, size_t len,
		bool settle);

/* Original .text0x14e0 has four seven-byte packets: wire on/off=1/10,
 * wireless on/off=2/20. Register and packet checksums are literal OEM values.
 */
static int meizu_charge(struct goodix_ts_core *cd, unsigned int mode)
{
	static const u8 commands[4][7] = {
		{ 0, 0, 5, 0xaf, 1, 0xb5, 0 },
		{ 0, 0, 5, 0xaf, 0, 0xb4, 0 },
		{ 0, 0, 5, 0xc6, 1, 0xcc, 0 },
		{ 0, 0, 5, 0xc6, 0, 0xcb, 0 },
	};
	unsigned int index;

	switch (mode) {
	case 1: index = 0; break;
	case 10: index = 1; break;
	case 2: index = 2; break;
	case 20: index = 3; break;
	default: return 0; /* OEM ignores undefined modes. */
	}
	return meizu_write(cd, commands[index], 7, true);
}

static void meizu_charge_work(struct work_struct *work)
{
	struct goodix_meizu *mz = container_of(work, struct goodix_meizu, charge_work);

	mutex_lock(&mz->state_lock);
	if (!mz->stopping)
		meizu_charge(mz->cd, mz->charge_mode);
	mutex_unlock(&mz->state_lock);
}

static void meizu_unplug_work(struct work_struct *work)
{
	struct goodix_meizu *mz = container_of(work, struct goodix_meizu, unplug_work);

	mutex_lock(&mz->state_lock);
	if (!mz->stopping)
		meizu_charge(mz->cd, mz->charge_mode);
	mutex_unlock(&mz->state_lock);
}

static int meizu_power_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct goodix_meizu *mz = container_of(nb, struct goodix_meizu, power_notifier);
	static const unsigned int modes[] = { 1, 10, 2, 20 };

	if (event >= ARRAY_SIZE(modes) || mz->cd->init_stage < CORE_INIT_STAGE2)
		return NOTIFY_DONE;
	mutex_lock(&mz->state_lock);
	if (!mz->stopping) {
		mz->charge_mode = modes[event];
		/* Original .text0x8340 records state while suspended and programs
		 * hardware only while awake; resume reapplies the last saved mode.
		 */
		if (!atomic_read(&mz->cd->suspended))
			queue_work(mz->charge_wq, event & 1 ? &mz->unplug_work : &mz->charge_work);
	}
	mutex_unlock(&mz->state_lock);
	return NOTIFY_DONE;
}

void goodix_meizu_resume_complete(struct goodix_ts_core *cd)
{
	mutex_lock(&cd->meizu->state_lock);
	if (!cd->meizu->stopping)
		meizu_charge(cd, cd->meizu->charge_mode);
	mutex_unlock(&cd->meizu->state_lock);
}

void goodix_meizu_input_capabilities(struct input_dev *input)
{
	static const unsigned int codes[] = {
		0x270, 0x271, 0x272, 0x280, 0x281, 0x282, 0x283, 0x290,
		0x291, 0x292, 0x293, 0x294, 0x295, 0x296, 0x29a,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		input_set_capability(input, EV_KEY, codes[i]);
}

int goodix_meizu_suspend(struct goodix_ts_core *cd)
{
	struct goodix_meizu *mz = cd->meizu;
	bool fod = READ_ONCE(FOD_switch_enable);
	bool gestures = READ_ONCE(Gesture_switch_enable);
	int ret;

	mutex_lock(&mz->state_lock);
	if (mz->stopping) {
		mutex_unlock(&mz->state_lock);
		return 0;
	}
	if (mz->normal_pressed) {
		mz_gesture_report(cd->input_dev, 0x272, 0);
		mz->normal_pressed = false;
		cancel_delayed_work(&mz->force_release);
	}
	mutex_unlock(&mz->state_lock);
	if (!fod && !gestures)
		return 0;
	ret = goodix_meizu_gesture(cd, fod ? (gestures ? 1 : 2) : 3);
	if (ret < 0)
		return ret;
	ret = enable_irq_wake(cd->irq);
	if (ret)
		return ret;
	mz->wake_enabled = true;
	cd->hw_ops->irq_enable(cd, true);
	return 1;
}

bool goodix_meizu_resume(struct goodix_ts_core *cd)
{
	struct goodix_meizu *mz = cd->meizu;
	bool was_awake = mz->wake_enabled;

	if (mz->wake_enabled) {
		disable_irq_wake(cd->irq);
		mz->wake_enabled = false;
	}
	mutex_lock(&mz->state_lock);
	/* Original .text 0x8d98: keep an LP press held across display wakeup,
	 * transfer its ownership to the normal event stream without another DOWN.
	 */
	if (mz->lp_pressed) {
		mz->lp_pressed = false;
		mz->normal_pressed = true;
	}
	mutex_unlock(&mz->state_lock);
	return was_awake;
}

static void meizu_reenable_work(struct work_struct *work)
{
	struct goodix_meizu *mz = container_of(to_delayed_work(work),
			struct goodix_meizu, reenable);

	mutex_lock(&mz->state_lock);
	if (!mz->stopping)
		goodix_meizu_fod(mz->cd, true);
	mutex_unlock(&mz->state_lock);
}

static void meizu_force_release_work(struct work_struct *work)
{
	struct goodix_meizu *mz = container_of(to_delayed_work(work),
			struct goodix_meizu, force_release);

	mutex_lock(&mz->state_lock);
	if (!mz->stopping) {
		/* Original .text 0x7c70: key release after 3000 ms, then cycle GIO4
		 * only while suspended and re-enable it after another 1000 ms.
		 */
		mz_gesture_report(mz->cd->input_dev, 0x272, 0);
		mz->lp_pressed = false;
		mz->fod_active = false;
		if (atomic_read(&mz->cd->suspended)) {
			goodix_meizu_fod(mz->cd, false);
			mod_delayed_work(system_wq, &mz->reenable, msecs_to_jiffies(1000));
		}
	}
	mutex_unlock(&mz->state_lock);
}

void goodix_meizu_normal_event(struct goodix_ts_core *cd,
		struct goodix_ts_event *event, bool invert_xy)
{
	struct goodix_meizu *mz = cd->meizu;
	struct goodix_touch_data *touch = &event->touch_data;
	struct input_dev *input = cd->input_dev;
	bool pressed = event->meizu_fod_pressed;
	unsigned int i, x, y;

	mutex_lock(&mz->state_lock);
	if (!mz->stopping && !atomic_read(&cd->suspended)) {
		mutex_lock(&input->mutex);
		for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
			input_mt_slot(input, i);
			input_mt_report_slot_state(input, MT_TOOL_FINGER,
					touch->coords[i].status == TS_TOUCH);
			if (touch->coords[i].status != TS_TOUCH)
				continue;
			x = touch->coords[i].x;
			y = touch->coords[i].y;
			if (invert_xy)
				swap(x, y);
			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
			input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch->coords[i].w);
		}
		/* Exact byte counters at OEM core offsets1672/1674/1675. BTN_TOUCH
		 * is emitted only in frames9/10 with no active LP fingerprint; its
		 * counter then decrements before processing the current FOD edge.
		 */
		if (!mz->fod_active && (mz->report_frames == 9 || mz->report_frames == 10)) {
			input_report_key(input, BTN_TOUCH, touch->touch_num != 0);
			input_sync(input);
			mz->report_frames--;
		} else if (mz->report_frames >= 11) {
			mz->report_frames--;
		}
		/* Original core .text 0x2d1c,0x30f0,0x3158: the header bit is
		 * authoritative; normal DOWN is suppressed while LP FOD is active.
		 */
		if (pressed && !mz->normal_pressed && !mz->fod_active) {
			mz_gesture_report(cd->input_dev, 0x272, 1);
			input_sync(input);
			mz->normal_pressed = true;
			mz->lp_origin = false;
			mod_delayed_work(system_wq, &mz->force_release,
					msecs_to_jiffies(3000));
		} else if (!pressed && mz->normal_pressed) {
			mz_gesture_report(cd->input_dev, 0x272, 0);
			input_sync(input);
			mz->normal_pressed = false;
			mz->fod_active = false;
			mz->single_frames = 0;
			mz->report_frames = mz->lp_origin ? 0 : 9;
			cancel_delayed_work(&mz->force_release);
		}
		if (touch->touch_num == 1)
			mz->single_frames++;
		mz->report_frames = ((u8)(mz->report_frames + 1) < 4 &&
				!mz->single_frames) ? 8 : mz->report_frames + 1;
		mutex_unlock(&input->mutex);
	}
	mutex_unlock(&mz->state_lock);
}

void goodix_meizu_lp_event(struct goodix_ts_core *cd,
		const struct goodix_ts_event *event)
{
	struct goodix_meizu *mz = cd->meizu;

	mutex_lock(&mz->state_lock);
	if (mz->stopping)
		goto out;
	switch (event->gesture_type) {
	case 0x46:
		/* .text 0x12e88: LP position/area is reported before the FOD key. */
		input_mt_slot(cd->input_dev, 0);
		input_mt_report_slot_state(cd->input_dev, MT_TOOL_FINGER, true);
		input_report_abs(cd->input_dev, ABS_MT_POSITION_X,
				get_unaligned_le16(event->gesture_data));
		input_report_abs(cd->input_dev, ABS_MT_POSITION_Y,
				get_unaligned_le16(event->gesture_data + 2));
		input_report_abs(cd->input_dev, ABS_MT_WIDTH_MAJOR, event->gesture_data[4]);
		input_sync(cd->input_dev);
		if (!mz->lp_pressed && !mz->fod_active) {
			mz_gesture_report(cd->input_dev, 0x272, 1);
			mz->lp_pressed = true;
			mz->fod_active = true;
			mz->lp_origin = true;
			mz->report_frames = 0;
			mod_delayed_work(system_wq, &mz->force_release,
					msecs_to_jiffies(3000));
		}
		break;
	case 0x55:
		if (mz->lp_pressed) {
			mz_gesture_report(cd->input_dev, 0x272, 0);
			mz->lp_pressed = false;
			mz->fod_active = false;
			mz->lp_origin = false;
			mz->report_frames = 10;
			cancel_delayed_work(&mz->force_release);
		}
		break;
	case 0x4c:
		mz_gesture_report(cd->input_dev, 0x271, 2);
		break;
	case 0xcc:
		if (mz_gesture_report(cd->input_dev, 0x270, 2)) {
			/* Flyme turned the panel on from its own 0x270 key, which
			 * AOSP ignores. Wake the framework with KEY_WAKEUP, like
			 * the generic Goodix double-tap path does.
			 */
			input_report_key(cd->input_dev, KEY_WAKEUP, 1);
			input_sync(cd->input_dev);
			input_report_key(cd->input_dev, KEY_WAKEUP, 0);
			input_sync(cd->input_dev);
		}
		break;
	/* Original jump tables .rodata+0x6c and +0xe4, and IRQ .text0x12cd4. */
	case 0xaa:
		mz_gesture_report(cd->input_dev, 0x281, 2);
		break;
	case 0xab:
		mz_gesture_report(cd->input_dev, 0x283, 2);
		break;
	case 0xba:
		mz_gesture_report(cd->input_dev, 0x282, 2);
		break;
	case 0xbb:
		mz_gesture_report(cd->input_dev, 0x280, 2);
		break;
	case 0x63:
		mz_gesture_report(cd->input_dev, 0x291, 2);
		break;
	}
out:
	mutex_unlock(&mz->state_lock);
}

static int meizu_write(struct goodix_ts_core *cd, const u8 *cmd, size_t len,
		bool settle)
{
	int ret;

	mutex_lock(&cd->meizu->command_lock);
	ret = READ_ONCE(cd->meizu->stopping) ? -ENODEV :
		cd->hw_ops->write(cd, MEIZU_COMMAND_REG, (u8 *)cmd, len);
	if (!ret && settle)
		msleep(20);
	mutex_unlock(&cd->meizu->command_lock);
	return ret;
}

int goodix_meizu_gesture(struct goodix_ts_core *cd, unsigned int mode)
{
	static const u8 commands[4][8] = {
		{ 0, 0, 6, 0xa6, 0xff, 0x3f, 0xea, 1 }, /* all disabled */
		{ 0, 0, 6, 0xa6, 0, 0, 0xac, 0 },       /* all enabled */
		{ 0, 0, 6, 0xa6, 0xff, 0x1f, 0xca, 1 }, /* fingerprint only */
		{ 0, 0, 6, 0xa6, 0, 0x20, 0xcc, 0 },   /* gestures only */
	};

	if (!cd->meizu || mode >= ARRAY_SIZE(commands))
		return -EINVAL;
	return meizu_write(cd, commands[mode], 8, true);
}

int goodix_meizu_fod(struct goodix_ts_core *cd, bool enabled)
{
	static const u8 commands[2][8] = {
		{ 0, 0, 6, 0xa6, 0, 0x60, 0x2c, 1 },
		{ 0, 0, 6, 0xa6, 0, 0x40, 0x0c, 1 },
	};

	if (!cd->meizu)
		return -EINVAL;
	return meizu_write(cd, commands[enabled], 8, true);
}

static ssize_t gesture_info_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	unsigned int mode;
	int ret = kstrtouint(buf, 0, &mode);

	if (ret || mode > 3)
		return -EINVAL;
	ret = goodix_meizu_gesture(cd, mode);
	return ret < 0 ? ret : count;
}

static ssize_t fod_report_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	static const u8 commands[2][8] = {
		{ 0, 0, 6, 0xa6, 0x80, 0x10, 0x5c, 1 },
		{ 0, 0, 6, 0xa6, 0x80, 0x10, 0x3c, 1 },
	};
	unsigned int enabled;
	int ret = kstrtouint(buf, 0, &enabled);

	if (ret || enabled > 1)
		return -EINVAL;
	ret = meizu_write(cd, commands[enabled], 8, true);
	if (ret < 0)
		return ret;
	WRITE_ONCE(FOD_switch_enable, enabled);
	return count;
}

static ssize_t goodix_ts_FOD_node_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	static const u8 commands[2][7] = {
		{ 0, 0, 5, 0x14, 1, 0x1a, 0 },
		{ 0, 0, 5, 0x14, 0, 0x19, 0 },
	};
	unsigned int enabled;
	int ret = kstrtouint(buf, 0, &enabled);

	if (ret || enabled > 1)
		return -EINVAL;
	ret = meizu_write(cd, commands[enabled], 7, false);
	return ret < 0 ? ret : count;
}

static ssize_t charge_enable_node_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	/* Original .text0x406c sysfs ABI differs from notifier mode values and
	 * even its wire-off checksum differs from goodix_charge_function.
	 */
	static const u8 commands[4][7] = {
		{ 0, 0, 5, 0xaf, 0, 0xb5, 0 },
		{ 0, 0, 5, 0xaf, 1, 0xb5, 0 },
		{ 0, 0, 5, 0xc6, 0, 0xcb, 0 },
		{ 0, 0, 5, 0xc6, 1, 0xcc, 0 },
	};
	unsigned int mode, index;
	int ret = kstrtouint(buf, 0, &mode);

	if (ret)
		return -EINVAL;
	switch (mode) {
	case 0: index = 0; break;
	case 1: index = 1; break;
	case 10: index = 2; break;
	case 11: index = 3; break;
	default: return count;
	}
	ret = meizu_write(cd, commands[index], 7, false);
	return ret < 0 ? ret : count;
}

/* Original nodes have mode 0220 and no show handler. */
static DEVICE_ATTR(gesture_info, 0220, NULL, gesture_info_store);
static DEVICE_ATTR(fod_report, 0220, NULL, fod_report_store);
static DEVICE_ATTR(goodix_ts_FOD_node, 0220, NULL, goodix_ts_FOD_node_store);
static DEVICE_ATTR(charge_enable_node, 0220, NULL, charge_enable_node_store);
static struct attribute *meizu_attrs[] = {
	&dev_attr_gesture_info.attr,
	&dev_attr_fod_report.attr,
	&dev_attr_goodix_ts_FOD_node.attr,
	&dev_attr_charge_enable_node.attr,
	NULL,
};
static const struct attribute_group meizu_group = { .attrs = meizu_attrs };

int goodix_meizu_init(struct goodix_ts_core *cd)
{
	struct goodix_meizu *mz;
	int ret;

	if (!of_device_is_compatible(cd->bus->dev->of_node, "goodix,brl-d"))
		return 0;
	mz = devm_kzalloc(&cd->pdev->dev, sizeof(*mz), GFP_KERNEL);
	if (!mz)
		return -ENOMEM;
	mutex_init(&mz->command_lock);
	mutex_init(&mz->state_lock);
	mz->cd = cd;
	INIT_DELAYED_WORK(&mz->force_release, meizu_force_release_work);
	INIT_DELAYED_WORK(&mz->reenable, meizu_reenable_work);
	INIT_WORK(&mz->charge_work, meizu_charge_work);
	INIT_WORK(&mz->unplug_work, meizu_unplug_work);
	mz->charge_wq = alloc_ordered_workqueue("goodix_meizu_charge", WQ_MEM_RECLAIM);
	if (!mz->charge_wq) {
		devm_kfree(&cd->pdev->dev, mz);
		return -ENOMEM;
	}
	/* IRQ has already been requested: publish only a fully initialized state. */
	smp_store_release(&cd->meizu, mz);
	ret = sysfs_create_group(&cd->pdev->dev.kobj, &meizu_group);
	if (ret)
		goto fail;
	mz->group_created = true;
	ret = meizu_sysfslink_register_name(&cd->pdev->dev, "main_tp");
	if (ret) {
		sysfs_remove_group(&cd->pdev->dev.kobj, &meizu_group);
		mz->group_created = false;
		goto fail;
	}
	cd->meizu->linked = true;
	mz->power_notifier.notifier_call = meizu_power_event;
	ret = register_mpower_notifier(&mz->power_notifier);
	if (ret) {
		meizu_sysfslink_remove_name("main_tp");
		mz->linked = false;
		sysfs_remove_group(&cd->pdev->dev.kobj, &meizu_group);
		mz->group_created = false;
		goto fail;
	}
	mz->power_registered = true;
	return 0;
fail:
	/* An IRQ may have observed this pointer. Keep its memory until devres
	 * release rather than freeing a concurrently accessible lock/work object.
	 */
	mutex_lock(&mz->state_lock);
	mz->stopping = true;
	mutex_unlock(&mz->state_lock);
	cancel_delayed_work_sync(&mz->force_release);
	cancel_delayed_work_sync(&mz->reenable);
	destroy_workqueue(mz->charge_wq);
	mz->charge_wq = NULL;
	return ret;
}

void goodix_meizu_exit(struct goodix_ts_core *cd)
{
	if (!cd->meizu)
		return;
	if (cd->meizu->power_registered)
		unregister_mpower_notifier(&cd->meizu->power_notifier);
	if (cd->meizu->linked)
		meizu_sysfslink_remove_name("main_tp");
	if (cd->meizu->wake_enabled) {
		disable_irq_wake(cd->irq);
		cd->meizu->wake_enabled = false;
	}
	if (cd->meizu->group_created)
		sysfs_remove_group(&cd->pdev->dev.kobj, &meizu_group);
	mutex_lock(&cd->meizu->state_lock);
	cd->meizu->stopping = true;
	mutex_unlock(&cd->meizu->state_lock);
	cancel_delayed_work_sync(&cd->meizu->force_release);
	cancel_delayed_work_sync(&cd->meizu->reenable);
	if (cd->meizu->charge_wq)
		destroy_workqueue(cd->meizu->charge_wq);
}
