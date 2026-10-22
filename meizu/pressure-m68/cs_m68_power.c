// SPDX-License-Identifier: GPL-2.0-only
/* GPIO transitions and retries recovered from cs_power_ctrl/power_reset. */
#include <linux/delay.h>
#include <linux/gpio.h>

#include "cs_m68.h"

#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

int cs_m68_prepare_power(struct cs_m68 *cs)
{
	struct device *dev = &cs->client->dev;
	int ret;

	cs->irq_gpio = of_get_named_gpio(dev->of_node, "irq-gpio", 0);
	if (cs->irq_gpio < 0)
		return cs->irq_gpio;
	ret = devm_gpio_request(dev, cs->irq_gpio, "cs-press-eint");
	if (ret)
		return ret;
	cs->enable_gpio = of_get_named_gpio(dev->of_node, "enable1v8_gpio", 0);
	if (cs->enable_gpio < 0)
		return cs->enable_gpio;
	ret = devm_gpio_request(dev, cs->enable_gpio, "enable1v8_gpio");
	if (ret)
		return ret;
	cs->enable_gpio_owned = true;
	/* Stock explicitly drives raw high despite the active-low DT flag. */
	ret = gpio_direction_output(cs->enable_gpio, 1);
	if (ret)
		return ret;
	cs->powered_down = false;
	msleep(40);
	cs->vdd = devm_regulator_get(dev, "vdd_2v8");
	if (IS_ERR(cs->vdd)) {
		ret = PTR_ERR(cs->vdd);
		gpio_set_value(cs->enable_gpio, 0);
		cs->powered_down = true;
		return ret;
	}
	ret = regulator_enable(cs->vdd);
	if (ret) {
		gpio_set_value(cs->enable_gpio, 0);
		cs->powered_down = true;
		return ret;
	}
	cs->vdd_enabled = true;
	msleep(40);
	return 0;
}

void cs_m68_release_power(struct cs_m68 *cs)
{
	if (cs->enable_gpio_owned)
		gpio_set_value(cs->enable_gpio, 0);
	cs->powered_down = true;
	if (cs->vdd_enabled) {
		regulator_disable(cs->vdd);
		cs->vdd_enabled = false;
	}
}

int cs_m68_power(struct cs_m68 *cs, bool enable)
{
	int attempt;

	if (!gpio_is_valid(cs->enable_gpio))
		return -EINVAL;
	if (cs->powered_down != enable)
		return -EALREADY;
	for (attempt = 0; attempt < 20; attempt++) {
		gpio_set_value(cs->enable_gpio, enable);
		if (gpio_get_value(cs->enable_gpio) == enable) {
			cs->powered_down = !enable;
			return 0;
		}
	}
	return -EIO;
}

int cs_m68_power_reset(struct cs_m68 *cs)
{
	int attempt, ret;

	for (attempt = 0; attempt < 3; attempt++) {
		ret = cs_m68_power(cs, false);
		msleep(100);
		if (!ret)
			break;
	}
	for (attempt = 0; attempt < 3; attempt++) {
		ret = cs_m68_power(cs, true);
		if (!ret) {
			msleep(50);
			return 0;
		}
		msleep(100);
	}
	return ret;
}

int cs_m68_check_application(struct cs_m68 *cs)
{
	u8 identity;
	int attempt, ret = -ENODEV;

	for (attempt = 0; attempt < 4; attempt++) {
		identity = 0;
		ret = cs_m68_read(cs, 0x00, &identity, 1);
		if (ret >= 0 && identity == 0x68)
			return 0;
		msleep(200);
	}
	return ret < 0 ? ret : -ENODEV;
}
