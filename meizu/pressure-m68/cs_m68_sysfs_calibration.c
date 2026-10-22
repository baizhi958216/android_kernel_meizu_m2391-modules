// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sysfs.h>

#include "cs_m68.h"

static ssize_t cal_data_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	char text[257] = { 0 };
	u32 value;
	u8 zero = 0;
	int ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	ret = cs_m68_read_calibration_text(cs, text, sizeof(text));
	if (ret) {
		ret = 0;
		goto out;
	}
	ret = scnprintf(buffer, 256, "%s\n", text);
	/* Stock replaces the record text with hardware-calibrated status when
	 * the FB/FC/FD response and its checksum are valid.
	 */
	if (!cs_m68_read_calibration(cs, &value))
		ret = sysfs_emit(buffer, "%u\n", !!value);
	mutex_lock(&cs->command_lock);
	cs_m68_write(cs, 0xfc, &zero, 1);
	mutex_unlock(&cs->command_lock);
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t cal_data_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&cs->access_lock);
	ret = cs->removed ? -ENODEV : cs_m68_write_calibration_text(cs, buffer, count);
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static ssize_t cali_param_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&cs->access_lock);
	ret = sysfs_emit(buffer, "%u %u ", cs->calibration_value & 0xffff,
		cs->calibration_value >> 16);
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t cali_param_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u32 values[3] = { 0, 100, 0 };
	static const u8 digits[] = { 2, 8, 2 };
	unsigned int command, value, channel;
	int ret;
	u32 result;

	/* OEM command form: 0x01 0x00000064 0x00 (write), 0x02 ... (read).
	 * Parse an owned, bounded sysfs buffer without stock's in-place stores
	 * past the token ends. Omitted value/channel use its original defaults.
	 */
	ret = cs_m68_parse_hex_tokens(buffer, count, values, digits, ARRAY_SIZE(values));
	if (ret < 0)
		return ret;
	command = values[0];
	value = values[1];
	channel = values[2];
	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	if (command == 1) {
		ret = cs_m68_set_calibration(cs, value, (u8)channel);
	} else if (command == 2) {
		ret = cs_m68_read_calibration(cs, &result);
		if (!ret) {
			cs->calibration_value = result;
			cs->calibration_channel = channel;
		}
	} else {
		ret = -EINVAL;
	}
out:
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static DEVICE_ATTR(cal_data, 0664, cal_data_show, cal_data_store);
static DEVICE_ATTR_RW(cali_param);

static struct attribute *cs_m68_calibration_attributes[] = {
	&dev_attr_cal_data.attr,
	&dev_attr_cali_param.attr,
	NULL,
};

const struct attribute_group cs_m68_calibration_group = {
	.attrs = cs_m68_calibration_attributes,
};
