// SPDX-License-Identifier: GPL-2.0-only
/* M2391 camera HAL flash power and dispatch, recovered from camera.ko. */
#include <linux/of.h>
#include <linux/gpio.h>
#include "../../../../../../meizu/system/meizu_sys.h"
#include "cam_flash_meizu.h"
#include "cam_flash_meizu_provider.h"

static DEFINE_MUTEX(meizu_flash_power_lock);
static unsigned int meizu_flash_power_users;

bool cam_flash_meizu_matches(struct cam_flash_ctrl *ctrl)
{
	struct device_node *root, *source, *parent;
	const char *version;
	bool matches = false;
	int count, i;

	if (!ctrl || !ctrl->soc_info.dev)
		return false;
	root = of_find_node_by_path("/");
	if (root && !of_property_read_string(root, "meizu,chip-version", &version))
		matches = !strncmp(version, "M2391", 5);
	of_node_put(root);
	if (!matches)
		return false;
	count = of_count_phandle_with_args(ctrl->soc_info.dev->of_node,
		"flash-source", NULL);
	if (count < 1 || count > 2)
		return false;
	for (i = 0; i < count; i++) {
		source = of_parse_phandle(ctrl->soc_info.dev->of_node, "flash-source", i);
		parent = of_get_parent(source);
		matches = parent && of_device_is_compatible(parent, "si,sy7808");
		of_node_put(parent);
		of_node_put(source);
		if (!matches)
			return false;
	}
	return true;
}

/* Stock cam_flash_power shares GPIO requests across all three flash camera
 * nodes. First acquire requests and drives the DT table high; last release
 * drives it low and frees it. Failed acquire rolls back its new references.
 */
static int meizu_flash_power(struct cam_flash_ctrl *ctrl, bool enable)
{
	struct cam_soc_gpio_data *gpios = ctrl->soc_info.gpio_data;
	struct gpio *table;
	unsigned int count, i;
	int rc = 0;

	if (!gpios)
		return 0;
	table = gpios->cam_gpio_req_tbl;
	count = gpios->cam_gpio_req_tbl_size;
	if (!table || !count)
		return -EINVAL;
	mutex_lock(&meizu_flash_power_lock);
	if (enable) {
		if (meizu_flash_power_users++)
			goto out;
		for (i = 0; i < count; i++) {
			rc = gpio_request_one(table[i].gpio, table[i].flags, table[i].label);
			if (rc) {
				while (i)
					gpio_free(table[--i].gpio);
				meizu_flash_power_users = 0;
				goto out;
			}
		}
		for (i = 0; i < count; i++)
			gpio_set_value_cansleep(table[i].gpio, 1);
	} else if (meizu_flash_power_users && !--meizu_flash_power_users) {
		for (i = 0; i < count; i++)
			gpio_set_value_cansleep(table[i].gpio, 0);
		gpio_free_array(table, count);
	}
out:
	mutex_unlock(&meizu_flash_power_lock);
	return rc;
}

/* Stock cam_flash_apply_setting has no is_regulator_enabled gate. It is
 * separate from factory prepare and is used by NRT INIT_FIRE/OFF paths.
 */
int cam_flash_meizu_apply_power(struct cam_flash_ctrl *ctrl, bool enable)
{
	int i, rc;

	if (enable) {
		rc = meizu_flash_power(ctrl, true);
		if (rc)
			return rc;
	}
	for (i = 0; i < 3; i++) {
		rc = enable ? cam_meizu_flash_provider_init(i) :
			cam_meizu_flash_provider_shutdown(i);
		if (rc < 0 && rc != -ENODEV) {
			if (enable) {
				while (i--)
					cam_meizu_flash_provider_shutdown(i);
				meizu_flash_power(ctrl, false);
			}
			return -EINVAL;
		}
	}
	return enable ? 0 : meizu_flash_power(ctrl, false);
}

/* Stock prepare uses provider init for enable and shutdown for disable. */
int cam_flash_meizu_prepare(struct cam_flash_ctrl *ctrl, bool enable)
{
	int i, rc;

	if (ctrl->is_regulator_enabled == enable)
		return -EINVAL;
	if (enable) {
		rc = meizu_flash_power(ctrl, true);
		if (rc)
			return rc;
		for (i = 0; i < 3; i++) {
			rc = cam_meizu_flash_provider_init(i);
			if (rc == -ENODEV)
				continue;
			if (rc < 0) {
				while (i--)
					cam_meizu_flash_provider_shutdown(i);
				meizu_flash_power(ctrl, false);
				return -EINVAL;
			}
		}
	} else {
		for (i = 0; i < 3; i++) {
			rc = cam_meizu_flash_provider_shutdown(i);
			if (rc < 0 && rc != -ENODEV)
				return -EINVAL;
		}
		rc = meizu_flash_power(ctrl, false);
		if (rc)
			return rc;
	}
	ctrl->is_regulator_enabled = enable;
	return 0;
}

/* Original low/high operations configure per-source current then enable
 * all three physical LED channel bits. Exceeding max uses operational current.
 */
int cam_flash_meizu_ops(struct cam_flash_ctrl *ctrl,
		struct cam_flash_frame_setting *data, enum camera_flash_opcode op)
{
	struct cam_flash_private_soc *soc = ctrl->soc_info.soc_private;
	u32 mode, count, i, duty;
	int rc;

	if (!data || !soc)
		return -EINVAL;
	if (op == CAMERA_SENSOR_FLASH_OP_FIRELOW) {
		mode = 2;
		count = ctrl->torch_num_sources;
	} else {
		mode = 3;
		count = ctrl->flash_num_sources;
	}
	if (count > 2)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		duty = data->led_current_ma[i];
		if (mode == 2 && duty > soc->torch_max_current[i])
			duty = soc->torch_op_current[i];
		if (mode == 3 && duty > soc->flash_max_current[i])
			duty = soc->flash_op_current[i];
		rc = cam_meizu_flash_provider_duty(i, mode, duty);
		if (rc == -ENODEV)
			continue;
		if (rc < 0)
			return -EINVAL;
		rc = cam_meizu_flash_provider_enable(i, mode, 7);
		if (rc < 0)
			return -EINVAL;
	}
	return 0;
}

int cam_flash_meizu_off(struct cam_flash_ctrl *ctrl)
{
	int i, rc;

	for (i = 0; i < 3; i++) {
		rc = cam_meizu_flash_provider_disable(i, 7);
		if (rc < 0 && rc != -ENODEV)
			return -EINVAL;
	}
	return 0;
}

/* Original 40-entry factory current table, including its final 1050mA step. */
static const u32 meizu_flash_factory_levels[] = {
	25, 50, 75, 100, 125, 150, 175, 200, 225, 250,
	275, 300, 325, 350, 375, 400, 425, 450, 475, 500,
	525, 550, 575, 600, 625, 650, 675, 700, 725, 750,
	775, 800, 825, 850, 875, 900, 925, 950, 975, 1050,
};

int cam_flash_meizu_factory_all(struct cam_flash_ctrl *ctrl,
		bool *enabled, int duty)
{
	u32 mode;
	int i, rc;

	if (!ctrl || !enabled || duty >= (int)ARRAY_SIZE(meizu_flash_factory_levels))
		return -EINVAL;
	if (duty < 0) {
		for (i = 0; i < 3; i++) {
			rc = cam_meizu_flash_provider_disable(i, 7);
			if (rc < 0 && rc != -ENODEV) return -EINVAL;
		}
		if (*enabled && cam_flash_meizu_prepare(ctrl, false)) return -EINVAL;
		*enabled = false;
		return 0;
	}
	mode = duty < 7 ? 2 : 3;
	if (!*enabled && cam_flash_meizu_prepare(ctrl, true)) return -EINVAL;
	for (i = 0; i < 3; i++) {
		rc = cam_meizu_flash_provider_duty(i, mode, meizu_flash_factory_levels[duty]);
		if (rc == -ENODEV) continue;
		if (rc < 0 || cam_meizu_flash_provider_enable(i, mode, 7) < 0)
			return -EINVAL;
	}
	*enabled = true;
	return 0;
}

/* store_flash accepts enable, mode, led_id, duty. The second integer is
 * logged but the current-table index selects the actual torch/flash mode.
 * Preserve the original provider=led_id/4 and channel bit shift, including
 * its unusual boundary mapping; do not reinterpret this as groups of three.
 */
int cam_flash_meizu_factory_channels(struct cam_flash_ctrl *ctrl,
		struct cam_flash_meizu_factory *state, int enable, int mode,
		int led, int duty)
{
	u32 bit = 0, channels, provider;
	int i, rc;

	if (!ctrl || !state) return -EINVAL;
	if (enable == 0 || enable == 1) {
		/* Original code traps on invalid array indices or shift counts. */
		if (led < 1 || led > 9) return -EINVAL;
		provider = led / 4;
		bit = BIT(led - 1);
		channels = bit >> (provider * 3);
	}
	if (enable == 1 || enable == 2) {
		if (duty < 0 || duty >= (int)ARRAY_SIZE(meizu_flash_factory_levels))
			return -EINVAL;
		mode = duty < 7 ? 2 : 3;
		if (!state->powered && (enable == 2 || !state->channels)) {
			if (cam_flash_meizu_prepare(ctrl, true)) return -EINVAL;
			/* Stock enable=2 publishes powered only after all operations. */
			if (enable == 1) state->powered = true;
		}
		for (i = 0; i < 3; i++) {
			if (enable == 1 && i != provider) continue;
			rc = cam_meizu_flash_provider_duty(i, mode,
				meizu_flash_factory_levels[duty]);
			if (rc == -ENODEV) continue;
			state->channels |= enable == 1 ? bit : 7 << (i * 3);
			if (rc < 0 || cam_meizu_flash_provider_enable(i, mode,
				enable == 1 ? channels : 7) < 0) return -EINVAL;
		}
		if (enable == 2) state->powered = true;
	} else if (enable == 0) {
		rc = cam_meizu_flash_provider_disable(provider, channels);
		if (rc < 0 && rc != -ENODEV) return -EINVAL;
		state->channels &= ~bit;
		if (!state->channels && state->powered) {
			if (cam_flash_meizu_prepare(ctrl, false)) return -EINVAL;
			state->powered = false;
		}
	} else if (enable == 3 && state->powered) {
		for (i = 0; i < 3; i++) {
			channels = (state->channels >> (i * 3)) & 7;
			if (channels) {
				rc = cam_meizu_flash_provider_disable(i, channels);
				if (rc < 0 && rc != -ENODEV) return -EINVAL;
			}
			state->channels &= ~(7 << (i * 3));
		}
		if (cam_flash_meizu_prepare(ctrl, false)) return -EINVAL;
		state->powered = false;
	}
	return 0;
}

/* Exact 36 six-byte factory pattern instructions from LedsStepMode1. */
static const u8 meizu_flash_pattern[][6] = {
	{ 0, 0, 255, 255, 255, 0 },
	{ 0, 1, 255, 255, 255, 0 },
	{ 1, 0, 0, 7, 255, 0 },
	{ 1, 0, 1, 8, 255, 0 },
	{ 1, 0, 4, 15, 255, 0 },
	{ 1, 1, 0, 7, 255, 0 },
	{ 1, 1, 1, 8, 255, 0 },
	{ 1, 1, 4, 15, 255, 0 },
	{ 3, 255, 255, 255, 2, 0 },
	{ 3, 255, 255, 255, 0, 0 },
	{ 1, 0, 5, 11, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 1, 2, 2, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 1, 2, 3, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 1, 2, 12, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 0, 5, 17, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 0, 2, 12, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 0, 5, 10, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 1, 2, 25, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 1, 2, 26, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 1, 5, 20, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 0, 5, 21, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 1, 0, 2, 20, 255, 0 },
	{ 2, 255, 255, 255, 1, 0 },
	{ 3, 255, 255, 255, 1, 0 },
	{ 4, 255, 255, 255, 255, 0 }
};

/* Runs only in process context with flash_mutex held. Stock processes each
 * instruction until WAIT, then schedules the next interpreter pass. Its loop
 * counter jumps back only after reaching INT_MAX; preserve that condition.
 */
static void meizu_flash_pattern_run(struct cam_flash_meizu_pattern *pattern)
{
	const u8 *step;
	u32 argument, delay = 100;
	bool wait = false;

	while (pattern->running && !pattern->stopping && !wait) {
		if (pattern->index >= ARRAY_SIZE(meizu_flash_pattern)) pattern->index = 0;
		step = meizu_flash_pattern[pattern->index];
		argument = step[4] | step[5] << 8;
		switch (step[0]) {
		case 0:
			/* Original ignores repeated prepare failure and still owns power. */
			cam_flash_meizu_prepare(pattern->ctrl, true);
			pattern->powered = true;
			break;
		case 1:
			if ((step[4] & 0xfe) == 4) pattern->brightness = 64;
			cam_meizu_flash_provider_factory(step[1], step[2], step[3],
				step[4], pattern->brightness, 0);
			break;
		case 2:
			delay = (argument & 0xfe) == 4 ? 64 : 100;
			wait = true;
			break;
		case 3:
			if (!argument) pattern->saved_index = pattern->index;
			else if (argument == 2) pattern->counter = 0;
			else if (argument == 1 && pattern->counter++ >= INT_MAX)
				pattern->index = pattern->saved_index;
			break;
		case 4:
			if (pattern->powered) {
				cam_meizu_flash_provider_factory(0, 2, 0, 0, pattern->brightness, 0);
				cam_meizu_flash_provider_factory(1, 2, 0, 0, pattern->brightness, 0);
				cam_flash_meizu_prepare(pattern->ctrl, false);
				pattern->powered = false;
			}
			pattern->running = false;
			return;
		}
		pattern->index++;
	}
	if (pattern->running && !pattern->stopping)
		hrtimer_start(&pattern->timer, ms_to_ktime(delay), HRTIMER_MODE_REL);
}

static void meizu_flash_pattern_work(struct work_struct *work)
{
	struct cam_flash_meizu_pattern *pattern =
		container_of(work, struct cam_flash_meizu_pattern, work);

	mutex_lock(&pattern->ctrl->flash_mutex);
	meizu_flash_pattern_run(pattern);
	mutex_unlock(&pattern->ctrl->flash_mutex);
}

static enum hrtimer_restart meizu_flash_pattern_timer(struct hrtimer *timer)
{
	struct cam_flash_meizu_pattern *pattern =
		container_of(timer, struct cam_flash_meizu_pattern, timer);

	if (!READ_ONCE(pattern->stopping)) schedule_work(&pattern->work);
	return HRTIMER_NORESTART;
}

void cam_flash_meizu_pattern_init(struct cam_flash_meizu_pattern *pattern,
		struct cam_flash_ctrl *ctrl)
{
	memset(pattern, 0, sizeof(*pattern));
	pattern->ctrl = ctrl;
	INIT_WORK(&pattern->work, meizu_flash_pattern_work);
	hrtimer_init(&pattern->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pattern->timer.function = meizu_flash_pattern_timer;
}

/* Caller holds flash_mutex; matches store_flash4 ignoring input contents. */
void cam_flash_meizu_pattern_start(struct cam_flash_meizu_pattern *pattern)
{
	if (pattern->running || pattern->stopping) return;
	pattern->running = true;
	pattern->index = 0;
	pattern->counter = 0;
	meizu_flash_pattern_run(pattern);
}

/* Call after draining sysfs and BEFORE shutdown/free, without flash_mutex.
 * Publish stopping under the same mutex used by rearm, then drain timer/work.
 */
void cam_flash_meizu_pattern_stop(struct cam_flash_meizu_pattern *pattern)
{
	mutex_lock(&pattern->ctrl->flash_mutex);
	WRITE_ONCE(pattern->stopping, true);
	mutex_unlock(&pattern->ctrl->flash_mutex);
	hrtimer_cancel(&pattern->timer);
	cancel_work_sync(&pattern->work);
	mutex_lock(&pattern->ctrl->flash_mutex);
	if (pattern->powered) {
		cam_meizu_flash_provider_factory(0, 2, 0, 0, pattern->brightness, 0);
		cam_meizu_flash_provider_factory(1, 2, 0, 0, pattern->brightness, 0);
		cam_flash_meizu_prepare(pattern->ctrl, false);
		pattern->powered = false;
	}
	pattern->running = false;
	mutex_unlock(&pattern->ctrl->flash_mutex);
}

struct cam_flash_meizu_context {
	struct cam_flash_meizu_factory factory;
	struct cam_flash_meizu_pattern pattern;
	bool both_enabled;
};

static ssize_t meizu_flash_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* Original flash_power/flash/flash_both shows return zero bytes. */
	return !strcmp(attr->attr.name, "flash4") ? scnprintf(buf, PAGE_SIZE, "0\n") : 0;
}

static ssize_t meizu_flash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_flash_ctrl *ctrl = dev_get_drvdata(dev);
	struct cam_flash_meizu_context *context;
	int enable, mode, led, duty, rc = 0;

	if (!ctrl || !ctrl->meizu) return -ENODEV;
	context = ctrl->meizu;
	/* Require complete requests before any operation that changes hardware. */
	if (!strcmp(attr->attr.name, "flash")) {
		if (sscanf(buf, "%d %d %d %d", &enable, &mode, &led, &duty) != 4)
			return -EINVAL;
	} else if (strcmp(attr->attr.name, "flash4")) {
		if (sscanf(buf, "%d", &enable) != 1) return -EINVAL;
	}
	mutex_lock(&ctrl->flash_mutex);
	if (!strcmp(attr->attr.name, "flash_power"))
		rc = cam_flash_meizu_prepare(ctrl, !!enable);
	else if (!strcmp(attr->attr.name, "flash_both"))
		rc = cam_flash_meizu_factory_all(ctrl, &context->both_enabled, enable);
	else if (!strcmp(attr->attr.name, "flash"))
		rc = cam_flash_meizu_factory_channels(ctrl, &context->factory,
			enable, mode, led, duty);
	else
		cam_flash_meizu_pattern_start(&context->pattern);
	mutex_unlock(&ctrl->flash_mutex);
	return rc < 0 ? -EINVAL : count;
}

static DEVICE_ATTR(flash_power, 0664, meizu_flash_show, meizu_flash_store);
static DEVICE_ATTR(flash, 0664, meizu_flash_show, meizu_flash_store);
static DEVICE_ATTR(flash4, 0664, meizu_flash_show, meizu_flash_store);
static DEVICE_ATTR(flash_both, 0664, meizu_flash_show, meizu_flash_store);
static struct attribute *meizu_flash_attrs[] = {
	&dev_attr_flash_power.attr, &dev_attr_flash.attr,
	&dev_attr_flash4.attr, &dev_attr_flash_both.attr, NULL,
};
static const struct attribute_group meizu_flash_group = { .attrs = meizu_flash_attrs };

int cam_flash_meizu_sysfs_register(struct cam_flash_ctrl *ctrl)
{
	struct cam_flash_meizu_context *context;
	int rc;

	/* Original only publishes the cell-index zero factory device. */
	if (!cam_flash_meizu_matches(ctrl) || ctrl->soc_info.index != 0) return 0;
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context) return -ENOMEM;
	cam_flash_meizu_pattern_init(&context->pattern, ctrl);
	ctrl->meizu = context;
	rc = sysfs_create_group(&ctrl->soc_info.dev->kobj, &meizu_flash_group);
	if (rc) goto free_context;
	rc = meizu_sysfslink_register_name(ctrl->soc_info.dev, "flash");
	if (!rc) return 0;
	sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_flash_group);
	cam_flash_meizu_pattern_stop(&context->pattern);
	mutex_lock(&ctrl->flash_mutex);
	cam_flash_meizu_off(ctrl);
	if (ctrl->is_regulator_enabled) cam_flash_meizu_prepare(ctrl, false);
	mutex_unlock(&ctrl->flash_mutex);
free_context:
	ctrl->meizu = NULL;
	kfree(context);
	return rc;
}

void cam_flash_meizu_sysfs_unregister(struct cam_flash_ctrl *ctrl)
{
	struct cam_flash_meizu_context *context = ctrl->meizu;

	if (!context) return;
	meizu_sysfslink_remove_name("flash");
	sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_flash_group);
	/* Drain callbacks, timer and work before ctrl or provider teardown. */
	cam_flash_meizu_pattern_stop(&context->pattern);
	mutex_lock(&ctrl->flash_mutex);
	cam_flash_meizu_off(ctrl);
	if (ctrl->is_regulator_enabled) cam_flash_meizu_prepare(ctrl, false);
	ctrl->meizu = NULL;
	mutex_unlock(&ctrl->flash_mutex);
	kfree(context);
}
