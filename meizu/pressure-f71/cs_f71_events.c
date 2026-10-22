// SPDX-License-Identifier: GPL-2.0-only
/* Independent recovery of stock fml_input_dev_init/fml_key_report. */
#include <linux/errno.h>
#include <linux/lockdep.h>

#include "cs_f71.h"

static const unsigned int cs_f71_keycodes[] = { 702, 701 };

void cs_f71_release_keys_locked(struct cs_f71 *cs)
{
	unsigned int i;

	lockdep_assert_held(&cs->command_lock);
	for (i = 0; i < ARRAY_SIZE(cs->input); i++) {
		if (!cs->input[i] || !cs->pressed[i])
			continue;
		/* Stock blank work swaps key701/702, leaving actual pressed keys
		 * stuck. Release the same capability used by the event path.
		 */
		input_report_key(cs->input[i], cs_f71_keycodes[i], 0);
		input_sync(cs->input[i]);
		cs->pressed[i] = false;
	}
}

void cs_f71_input_exit(struct cs_f71 *cs)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cs->input); i++) {
		if (!cs->input[i])
			continue;
		if (cs->pressed[i]) {
			input_report_key(cs->input[i], cs_f71_keycodes[i], 0);
			input_sync(cs->input[i]);
			cs->pressed[i] = false;
		}
		input_unregister_device(cs->input[i]);
		cs->input[i] = NULL;
	}
}

int cs_f71_input_init(struct cs_f71 *cs)
{
	struct input_dev *input;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(cs->input); i++) {
		input = input_allocate_device();
		if (!input) {
			ret = -ENOMEM;
			goto fail;
		}
		/* Both original input devices have this name; top is key702,
		 * bottom is key701. No pressure ABS events are reported by F71.
		 */
		input->name = "ndt";
		input->dev.parent = &cs->client->dev;
		input_set_capability(input, EV_KEY, cs_f71_keycodes[i]);
		ret = input_register_device(input);
		if (ret) {
			input_free_device(input);
			goto fail;
		}
		cs->input[i] = input;
	}
	return 0;
fail:
	cs_f71_input_exit(cs);
	return ret;
}

void cs_f71_report_keys_locked(struct cs_f71 *cs)
{
	unsigned int i;
	int level;
	bool pressed;

	lockdep_assert_held(&cs->command_lock);
	for (i = 0; i < ARRAY_SIZE(cs->input); i++) {
		if (!cs->input[i] || IS_ERR_OR_NULL(cs->irq_gpio[i]))
			continue;
		level = gpiod_get_raw_value_cansleep(cs->irq_gpio[i]);
		if (level < 0)
			continue;
		pressed = !level;
		if (pressed == cs->pressed[i])
			continue;
		input_report_key(cs->input[i], cs_f71_keycodes[i], pressed);
		input_sync(cs->input[i]);
		cs->pressed[i] = pressed;
	}
}
