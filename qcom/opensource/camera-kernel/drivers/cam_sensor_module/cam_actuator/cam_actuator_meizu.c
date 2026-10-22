// SPDX-License-Identifier: GPL-2.0-only
/* M2391 AF factory interfaces recovered from original camera.ko. */
#include <linux/of.h>
#include <linux/delay.h>
#include "cam_actuator_core.h"
#include "cam_actuator_meizu.h"
#include "../../../../../../meizu/system/meizu_sys.h"

static char *meizu_af_link(u32 index)
{
	switch (index) {
	case 0: return "cam_af";
	case 1: return "cam_auxaf";
	case 3: return "cam_aux2af";
	default: return NULL;
	}
}

static ssize_t meizu_af_position_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_actuator_ctrl_t *ctrl = dev_get_drvdata(dev);
	int16_t position = -1, difference = 0;
	ssize_t length = 0;
	bool main;
	int rc;

	if (!ctrl)
		return sysfs_emit(buf, "result=fail\n");
	main = ctrl->soc_info.index == 0;
	mutex_lock(&ctrl->actuator_mutex);
	if (main && ctrl->meizu_enable_status < 0)
		length = sysfs_emit(buf, "result=fail, enable_status:%d.\n",
			ctrl->meizu_enable_status);
	else if (ctrl->meizu_requested_position > 1024)
		length = sysfs_emit(buf, "result=fail, invalid position.\n");
	else {
		rc = meizu_get_af_pos(ctrl, &position, 3);
		if (rc < 0) {
			mutex_unlock(&ctrl->actuator_mutex);
			return -EINVAL;
		}
		difference = (s16)(ctrl->meizu_requested_position - (u16)position);
	}
	if (main)
		length += sysfs_emit_at(buf, length,
			"current_af_position: %d, current_af_diff_position=%d\n",
			position, difference);
	else
		length += sysfs_emit_at(buf, length, "current_%s_position=%d\n",
			ctrl->soc_info.index == 1 ? "auxaf" : "aux2af", position);
	/* Original main checks signed difference; auxiliaries check position.
	 * Neither uses abs(), including when an earlier diagnostic was emitted.
	 */
	length += sysfs_emit_at(buf, length, "result=%s\n",
		(main ? difference : position) > 200 ? "fail" : "pass");
	mutex_unlock(&ctrl->actuator_mutex);
	return length;
}

static ssize_t meizu_af_position_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_actuator_ctrl_t *ctrl = dev_get_drvdata(dev);
	int requested = 0, rc;

	if (!ctrl)
		return -EINVAL;
	if (sscanf(buf, "%d", &requested) != 1)
		return -EINVAL;
	mutex_lock(&ctrl->actuator_mutex);
	/* Stock saves the truncated request even when validation rejects it. */
	ctrl->meizu_requested_position = (u16)requested;
	if ((u32)requested >= 1024) {
		rc = -EINVAL;
		goto out;
	}
	rc = meizu_set_af_pos(ctrl, requested);
	if (rc < 0)
		goto out;
	msleep(150);
out:
	mutex_unlock(&ctrl->actuator_mutex);
	return rc < 0 ? -EINVAL : count;
}

/* The original enable show is a literal zero-length read. */
static ssize_t af_enable_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return 0;
}

static ssize_t af_enable_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_actuator_ctrl_t *ctrl = dev_get_drvdata(dev);
	int enable = 0, rc;

	if (!ctrl)
		return -EINVAL;
	if (sscanf(buf, "%d", &enable) != 1)
		return -EINVAL;
	mutex_lock(&ctrl->actuator_mutex);
	rc = meizu_actuator_enable(ctrl, enable);
	ctrl->meizu_enable_status = rc;
	mutex_unlock(&ctrl->actuator_mutex);
	return rc < 0 ? -EINVAL : count;
}

static DEVICE_ATTR(af_pos, 0664, meizu_af_position_show, meizu_af_position_store);
static DEVICE_ATTR(auxaf_pos, 0664, meizu_af_position_show, meizu_af_position_store);
static DEVICE_ATTR(aux2af_pos, 0664, meizu_af_position_show, meizu_af_position_store);
static DEVICE_ATTR(af_enable, 0664, af_enable_show, af_enable_store);

static umode_t meizu_af_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct cam_actuator_ctrl_t *ctrl = dev_get_drvdata(kobj_to_dev(kobj));
	u32 index = ctrl->soc_info.index;

	if ((index == 0 && (attr == &dev_attr_af_pos.attr ||
		attr == &dev_attr_af_enable.attr)) ||
	    (index == 1 && attr == &dev_attr_auxaf_pos.attr) ||
	    (index == 3 && attr == &dev_attr_aux2af_pos.attr))
		return attr->mode;
	return 0;
}
static struct attribute *meizu_af_attrs[] = {
	&dev_attr_af_pos.attr, &dev_attr_af_enable.attr,
	&dev_attr_auxaf_pos.attr, &dev_attr_aux2af_pos.attr, NULL,
};
static const struct attribute_group meizu_af_group = {
	.attrs = meizu_af_attrs, .is_visible = meizu_af_visible,
};

int cam_actuator_meizu_sysfs_init(struct cam_actuator_ctrl_t *ctrl)
{
	char *link = meizu_af_link(ctrl->soc_info.index);
	const char *version;
	struct device_node *root;
	bool matches = false;
	int rc;

	if (!link)
		return 0;
	root = of_find_node_by_path("/");
	if (root && !of_property_read_string(root, "meizu,chip-version", &version))
		matches = !strncmp(version, "M2391", 5);
	of_node_put(root);
	if (!matches)
		return 0;
	rc = sysfs_create_group(&ctrl->soc_info.dev->kobj, &meizu_af_group);
	if (rc)
		return rc;
	rc = meizu_sysfslink_register_name(ctrl->soc_info.dev, link);
	if (rc) {
		sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_af_group);
		return rc;
	}
	ctrl->meizu_sysfs_ready = true;
	return 0;
}

void cam_actuator_meizu_sysfs_remove(struct cam_actuator_ctrl_t *ctrl)
{
	if (ctrl->meizu_sysfs_ready) {
		meizu_sysfslink_remove_name(meizu_af_link(ctrl->soc_info.index));
		sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_af_group);
		ctrl->meizu_sysfs_ready = false;
	}
}
