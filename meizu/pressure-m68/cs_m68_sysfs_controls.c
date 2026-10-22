// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/sysfs.h>

#include "cs_m68.h"

static ssize_t cs_gpio_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);

	return sysfs_emit(buffer, "reset gpio[%d] value %d\n", cs->enable_gpio,
		gpio_get_value(cs->enable_gpio));
}

static ssize_t cs_gpio_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int value, ret;

	if (kstrtoint(buffer, 10, &value))
		return -EINVAL;
	if (value != 0 && value != 1)
		return count;
	mutex_lock(&cs->access_lock);
	ret = cs->removed ? -ENODEV : cs_m68_power(cs, !!value);
	mutex_unlock(&cs->access_lock);
	return ret && ret != -EALREADY ? ret : count;
}

static ssize_t sleep_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u8 sleep, interrupt;
	int ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&cs->command_lock);
	ret = cs_m68_read(cs, 0x07, &sleep, 1);
	if (ret >= 0)
		ret = cs_m68_read(cs, 0x1e, &interrupt, 1);
	mutex_unlock(&cs->command_lock);
	if (ret >= 0)
		ret = sysfs_emit(buffer,
			"Get IC sleep status [%d] interrupt status [%d]\n",
			sleep, interrupt);
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t sleep_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int value, ret = 0;
	u8 byte;

	if (kstrtoint(buffer, 10, &value))
		return -EINVAL;
	byte = value;
	mutex_lock(&cs->access_lock);
	if (cs->removed)
		ret = -ENODEV;
	else if (cs->initialized) {
		mutex_lock(&cs->command_lock);
		ret = cs_m68_write(cs, 0x1e, &byte, 1);
		mutex_unlock(&cs->command_lock);
		msleep(10);
	}
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static ssize_t cs_irq_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);

	/* Original diagnostic label is unconditionally "Damage". */
	return sysfs_emit(buffer, "Irq gpio[%d] is Damage value %d\n",
		cs->irq_gpio, gpio_get_value(cs->irq_gpio));
}

static ssize_t cs_irq_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int value, ret;

	if (kstrtoint(buffer, 10, &value))
		return -EINVAL;
	/* 12/15 were unconditional disable/enable aliases. Avoid IRQ depth
	 * imbalance when these commands repeat; hardware state is unchanged.
	 */
	if (value != 0 && value != 1 && value != 12 && value != 15)
		return count;
	mutex_lock(&cs->access_lock);
	ret = cs->removed ? -ENODEV : cs_m68_set_irq(cs, value == 1 || value == 15);
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static ssize_t cs_m68_write_status(struct device *dev, const char *buffer,
		size_t count, u8 reg)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int value, ret;
	u8 byte;

	if (kstrtoint(buffer, 10, &value))
		return -EINVAL;
	byte = value;
	mutex_lock(&cs->access_lock);
	if (cs->removed)
		ret = -ENODEV;
	else {
		mutex_lock(&cs->command_lock);
		ret = cs_m68_write(cs, reg, &byte, 1);
		mutex_unlock(&cs->command_lock);
	}
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : min_t(size_t, count, 127);
}

static ssize_t set_sleep_status_store(struct device *dev,
		struct device_attribute *attr, const char *buffer, size_t count)
{
	return cs_m68_write_status(dev, buffer, count, 0x07);
}

static ssize_t set_interrupt_status_store(struct device *dev,
		struct device_attribute *attr, const char *buffer, size_t count)
{
	return cs_m68_write_status(dev, buffer, count, 0x1e);
}

static ssize_t set_log_status_store(struct device *dev,
		struct device_attribute *attr, const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int value, ret;

	if (kstrtoint(buffer, 10, &value))
		return -EINVAL;
	mutex_lock(&cs->access_lock);
	ret = cs->removed ? -ENODEV : cs_m68_save_log_mask(cs, (u8)value);
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static ssize_t shortcut_key_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	/* Stock exposes no text for this command-only compatibility attribute. */
	return 0;
}

static ssize_t shortcut_key_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int position, enabled, ret = 0;
	u8 byte;

	if (sscanf(buffer, "%d %d", &position, &enabled) != 2)
		return -EINVAL;
	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	if (cs->shortcut_position == position && cs->shortcut_enabled == enabled)
		goto out;
	byte = enabled ? (u8)position : 0;
	mutex_lock(&cs->command_lock);
	ret = cs_m68_write(cs, 0x10, &byte, 1);
	mutex_unlock(&cs->command_lock);
	if (ret >= 0) {
		cs->shortcut_position = position;
		cs->shortcut_enabled = enabled;
	}
out:
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static DEVICE_ATTR_RW(cs_gpio);
static DEVICE_ATTR_RW(sleep);
static DEVICE_ATTR_RW(cs_irq);
static DEVICE_ATTR_WO(set_sleep_status);
static DEVICE_ATTR_WO(set_interrupt_status);
static DEVICE_ATTR_WO(set_log_status);
static DEVICE_ATTR_RW(shortcut_key);

static struct attribute *cs_m68_control_attributes[] = {
	&dev_attr_cs_gpio.attr,
	&dev_attr_sleep.attr,
	&dev_attr_cs_irq.attr,
	&dev_attr_set_sleep_status.attr,
	&dev_attr_set_interrupt_status.attr,
	&dev_attr_set_log_status.attr,
	&dev_attr_shortcut_key.attr,
	NULL,
};

const struct attribute_group cs_m68_control_group = {
	.attrs = cs_m68_control_attributes,
};
