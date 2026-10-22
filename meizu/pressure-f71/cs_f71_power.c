// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of the stock cs_parse_dts GPIO/supply sequence. */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "cs_f71.h"

static struct gpio_desc *cs_f71_request_gpio(struct cs_f71 *cs,
		const char *property, unsigned long flags)
{
	int gpio, ret;

	gpio = of_get_named_gpio(cs->client->dev.of_node, property, 0);
	if (gpio < 0)
		return ERR_PTR(gpio);
	ret = devm_gpio_request_one(&cs->client->dev, gpio, flags, property);
	if (ret)
		return ERR_PTR(ret);
	/* Raw levels are intentional: stock ignores OF_ACTIVE_LOW for these
	 * legacy property names, including enable3v3_gpio.
	 */
	return gpio_to_desc(gpio);
}

int cs_f71_power_init(struct cs_f71 *cs)
{
	static const char * const irq_properties[] = { "irq0-gpio", "irq1-gpio" };
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(cs->irq_gpio); i++) {
		cs->irq_gpio[i] = cs_f71_request_gpio(cs, irq_properties[i], GPIOF_IN);
		if (IS_ERR_OR_NULL(cs->irq_gpio[i]))
			return cs->irq_gpio[i] ? PTR_ERR(cs->irq_gpio[i]) : -ENODEV;
		if (i == 1)
			msleep(50);
	}
	cs->reset_gpio = cs_f71_request_gpio(cs, "reset-gpio", GPIOF_OUT_INIT_LOW);
	if (IS_ERR_OR_NULL(cs->reset_gpio))
		return cs->reset_gpio ? PTR_ERR(cs->reset_gpio) : -ENODEV;
	msleep(50);
	cs->enable_gpio = cs_f71_request_gpio(cs, "enable3v3_gpio", GPIOF_OUT_INIT_LOW);
	if (IS_ERR_OR_NULL(cs->enable_gpio))
		return cs->enable_gpio ? PTR_ERR(cs->enable_gpio) : -ENODEV;
	msleep(50);
	gpiod_set_raw_value_cansleep(cs->enable_gpio, 1);
	cs->supply = devm_regulator_get(&cs->client->dev, "vdd_2v8");
	if (IS_ERR(cs->supply)) {
		ret = PTR_ERR(cs->supply);
		goto disable;
	}
	ret = regulator_enable(cs->supply);
	if (ret)
		goto disable;
	cs->supply_enabled = true;
	return 0;
disable:
	gpiod_set_raw_value_cansleep(cs->enable_gpio, 0);
	return ret;
}

void cs_f71_power_exit(struct cs_f71 *cs)
{
	if (cs->supply_enabled) {
		regulator_disable(cs->supply);
		cs->supply_enabled = false;
	}
	if (!IS_ERR_OR_NULL(cs->enable_gpio))
		gpiod_set_raw_value_cansleep(cs->enable_gpio, 0);
}
