// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <asm/unaligned.h>

#include "cs_m68.h"

static ssize_t get_raw_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u8 mode, zero = 0, one = 1, sample[6], sum;
	unsigned int length, i;
	int ret, reset_ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&cs->command_lock);
	cs_m68_wake(cs);
	ret = cs_m68_read(cs, 0x80, &mode, 1);
	if (ret < 0)
		goto reset;
	if (mode != 1) {
		ret = cs_m68_write(cs, 0x80, &one, 1);
		if (ret < 0)
			goto reset;
	}
	ret = cs_m68_read(cs, 0x81, sample, sizeof(sample));
	if (ret < 0)
		goto reset;
	if (!sample[0]) {
		ret = sysfs_emit(buffer, "read raw 80=0 ,\n");
		goto reset;
	}
	length = min_t(unsigned int, sample[0], 5);
	sum = 0;
	for (i = 1; i < length; i++)
		sum += sample[i];
	if (sum != sample[length])
		ret = 0;
	else
		ret = sysfs_emit(buffer, "%d %d\n",
			(s16)get_unaligned_le16(sample + 1),
			(s16)get_unaligned_le16(sample + 3));
reset:
	reset_ret = cs_m68_write(cs, 0x80, &zero, 1);
	if (ret >= 0 && reset_ret < 0)
		ret = reset_ret;
	mutex_unlock(&cs->command_lock);
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t get_raw_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	return count;
}

static int cs_m68_read_eeprom(struct cs_m68 *cs)
{
	u8 zero = 0, mode = 0x40, data[33];
	int ret;

	mutex_lock(&cs->command_lock);
	ret = cs_m68_write(cs, 0x80, &zero, 1);
	if (ret >= 0)
		ret = cs_m68_write(cs, 0x80, &mode, 1);
	if (ret >= 0) {
		msleep(30);
		ret = cs_m68_read(cs, 0x81, data, sizeof(data));
		if (ret >= 0)
			dev_info(&cs->client->dev, "EEPROM: %*ph\n", (int)sizeof(data), data);
	}
	mutex_unlock(&cs->command_lock);
	return ret < 0 ? ret : 0;
}

static ssize_t debug_th_show(struct device *dev, struct device_attribute *attr,
		char *buffer)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	switch (cs->debug_mode) {
	case 0:
		ret = cs_m68_read_eeprom(cs);
		if (!ret) {
			/* Stock returns one byte without initializing the output. */
			buffer[0] = '\0';
			ret = 1;
		}
		break;
	case 2:
		ret = sysfs_emit(buffer, "%d %d ", cs->threshold_down, cs->threshold_up);
		break;
	case 4:
		ret = sysfs_emit(buffer, "%d %d ", cs->vibrate_down, cs->vibrate_up);
		break;
	default:
		ret = sysfs_emit(buffer, "0 0 ");
		break;
	}
out:
	mutex_unlock(&cs->access_lock);
	return ret;
}

static ssize_t debug_th_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t count)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);
	u32 values[4] = { 0, 60, 50, 0 };
	static const u8 digits[] = { 2, 4, 4, 2 };
	unsigned int mode, first, second, channel;
	int ret = 0;

	/* Fixed-width OEM form is 0xNN 0xNNNN 0xNNNN 0xNN. */
	ret = cs_m68_parse_hex_tokens(buffer, count, values, digits, ARRAY_SIZE(values));
	if (ret < 0)
		return ret;
	mode = values[0];
	first = values[1];
	second = values[2];
	channel = values[3];
	ret = 0;
	mutex_lock(&cs->access_lock);
	if (cs->removed) {
		ret = -ENODEV;
		goto out;
	}
	switch (mode) {
	case 1:
		ret = cs_m68_set_threshold(cs, first, second, channel);
		break;
	case 2:
		ret = cs_m68_get_threshold(cs, &cs->threshold_down, &cs->threshold_up, channel);
		cs->debug_mode = 2;
		break;
	case 3:
		ret = cs_m68_set_vibrate_time(cs, first, second, channel);
		break;
	case 4:
		ret = cs_m68_get_vibrate_time(cs, &cs->vibrate_down, &cs->vibrate_up);
		cs->debug_mode = 4;
		break;
	default:
		cs->debug_mode = 0;
		break;
	}
out:
	mutex_unlock(&cs->access_lock);
	return ret < 0 ? ret : count;
}

static DEVICE_ATTR_RW(get_raw);
static DEVICE_ATTR_RW(debug_th);

static struct attribute *cs_m68_diagnostic_attributes[] = {
	&dev_attr_get_raw.attr,
	&dev_attr_debug_th.attr,
	NULL,
};

const struct attribute_group cs_m68_diagnostic_group = {
	.attrs = cs_m68_diagnostic_attributes,
};
