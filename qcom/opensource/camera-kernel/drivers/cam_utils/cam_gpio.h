/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CAM_GPIO_H
#define CAM_GPIO_H

#include <linux/gpio.h>

struct cam_gpio {
	unsigned int gpio;
	unsigned long flags;
	const char *label;
};

static inline void cam_gpio_free_array(const struct cam_gpio *gpios, size_t count)
{
	while (count)
		gpio_free(gpios[--count].gpio);
}

#endif
