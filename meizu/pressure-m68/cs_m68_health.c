// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include "cs_m68.h"

int cs_m68_set_irq(struct cs_m68 *cs, bool enabled)
{
	int ret = 0;

	mutex_lock(&cs->irq_lock);
	if (!cs->irq_live) {
		ret = -ENODEV;
		goto out;
	}
	if (cs->irq_enabled == enabled)
		goto out;
	if (enabled)
		enable_irq(cs->irq);
	else
		disable_irq_nosync(cs->irq);
	cs->irq_enabled = enabled;
out:
	mutex_unlock(&cs->irq_lock);
	return ret;
}

static int cs_m68_restart_application(struct cs_m68 *cs)
{
	u8 identity;
	int ret, enable_ret;

	ret = cs_m68_set_irq(cs, false);
	if (ret)
		return ret;
	ret = cs_m68_power_reset(cs);
	if (!ret) {
		ret = cs_m68_read(cs, 0x00, &identity, 1);
		if (ret >= 0)
			ret = cs_m68_update_skip_command(cs);
	}
	msleep(20);
	enable_ret = cs_m68_set_irq(cs, true);
	return ret < 0 ? ret : enable_ret;
}

int cs_m68_check_health(struct cs_m68 *cs)
{
	u8 first, second;
	int remaining, ret = -EIO, first_ret, second_ret;

	mutex_lock(&cs->command_lock);
	for (remaining = 4; remaining; remaining--) {
		first_ret = cs_m68_read(cs, 0x00, &first, 1);
		second_ret = cs_m68_read(cs, 0x00, &second, 1);
		/* Unlike the application firmware ID, this watchdog expects 0x66. */
		if (first_ret >= 0 && second_ret >= 0 && first == second) {
			if (first == 0x66) {
				ret = 0;
				break;
			}
			ret = cs_m68_restart_application(cs);
			if (ret)
				break;
		}
		/* Stock makes one additional recovery on its penultimate pass. */
		if (remaining == 2) {
			ret = cs_m68_restart_application(cs);
			if (ret)
				break;
		}
		ret = -EIO;
	}
	mutex_unlock(&cs->command_lock);
	return ret;
}
