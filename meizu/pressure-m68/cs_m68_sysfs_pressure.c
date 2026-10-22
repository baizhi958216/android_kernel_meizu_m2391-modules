// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sysfs.h>
#include <asm/unaligned.h>

#include "cs_m68.h"

static ssize_t fw_info_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u8 identity[10] = { 0 }, manufacturer[2], module[2], version[2];
	char text[21];
	int i, ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&cs->command_lock);
	cs_m68_wake(cs);
	if (cs_m68_read(cs, 0x02, identity, sizeof(identity)) < 0) {
		identity[0] = 0xff;
		identity[1] = 0xfe;
	}
	if (cs_m68_read(cs, 0x03, manufacturer, sizeof(manufacturer)) < 0) {
		manufacturer[0] = 0xff;
		manufacturer[1] = 0xfe;
	}
	if (cs_m68_read(cs, 0x04, module, sizeof(module)) < 0) {
		module[0] = 0xff;
		module[1] = 0xfe;
	}
	if (cs_m68_read(cs, 0x05, version, sizeof(version)) < 0) {
		version[0] = 0xff;
		version[1] = 0xfe;
	}
	mutex_unlock(&cs->command_lock);
	for (i = 0; i < sizeof(identity); i++)
		snprintf(text + 2 * i, 3, "%02X", identity[i]);
	ret = sysfs_emit(buffer,
		"Dev:%s,Manu:%02X%02X,Module:%02d%02d,FW:0x%02X%02X\n",
		text, manufacturer[1], manufacturer[0], module[1], module[0],
		version[1], version[0]);
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t fw_info_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	return count;
}

static ssize_t force_value_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u8 data[2];
	u16 value;
	int ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&cs->command_lock);
	cs_m68_wake(cs);
	ret = cs_m68_read(cs, 0x26, data, sizeof(data));
	mutex_unlock(&cs->command_lock);
	/* Original force_value reports this sentinel on an I2C error. */
	value = ret < 0 ? 0xfeff : get_unaligned_le16(data);
	ret = sysfs_emit(buffer, "%d\n", value);
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t force_value_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	/* The original callback deliberately consumes writes without a command. */
	return count;
}

static ssize_t get_press_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u8 state = 0;
	s16 pressure = -1;
	int ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	ret = cs_m68_read_pressure(cs, &state, &pressure);
	if (!state)
		ret = sysfs_emit(buffer, "TP no touch key location! ,\n");
	else
		ret = sysfs_emit(buffer, "%d , %d\n", state, pressure);
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t get_press_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	return count;
}

static DEVICE_ATTR_RW(force_value);
static DEVICE_ATTR_RW(get_press);
static DEVICE_ATTR_RW(fw_info);

static struct attribute *cs_m68_pressure_attributes[] = {
	&dev_attr_force_value.attr,
	&dev_attr_get_press.attr,
	&dev_attr_fw_info.attr,
	NULL,
};

const struct attribute_group cs_m68_pressure_group = {
	.attrs = cs_m68_pressure_attributes,
};
