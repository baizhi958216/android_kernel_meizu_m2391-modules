// SPDX-License-Identifier: GPL-2.0-only
/* M68 register protocol and GPIO-edge input reporting recovered from stock. */
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <asm/unaligned.h>

#include "cs_m68.h"

int cs_m68_read_pressure(struct cs_m68 *cs, u8 *state, s16 *pressure)
{
	u8 sample[3];
	int ret;

	if (!state || !pressure)
		return -EINVAL;
	mutex_lock(&cs->command_lock);
	cs_m68_wake(cs);
	ret = cs_m68_read(cs, 0x20, state, 1);
	if (ret < 0)
		goto out;
	if (!*state) {
		ret = -ENODATA;
		goto out;
	}
	ret = cs_m68_read(cs, 0x21, sample, sizeof(sample));
	if (ret < 0)
		goto out;
	if (sample[2] != (u8)(sample[0] + sample[1])) {
		ret = -EBADMSG;
		goto out;
	}
	*pressure = (s16)get_unaligned_le16(sample);
	ret = 0;
out:
	mutex_unlock(&cs->command_lock);
	return ret;
}

static void cs_m68_report_edge(struct cs_m68 *cs)
{
	int level = gpio_get_value(cs->irq_gpio);
	bool down;

	/* Stock uses the raw interrupt GPIO and fixed key 701, not reg 0x20. */
	if (level != 0 && level != 1)
		return;
	down = !level;
	mutex_lock(&cs->event_lock);
	if (down == cs->finger_down)
		goto out;
	input_report_key(cs->input, 701, down);
	input_sync(cs->input);
	cs->finger_down = down;
out:
	mutex_unlock(&cs->event_lock);
}

void cs_m68_release_key(struct cs_m68 *cs)
{
	mutex_lock(&cs->event_lock);
	if (cs->finger_down) {
		input_report_key(cs->input, 701, 0);
		input_sync(cs->input);
		cs->finger_down = false;
	}
	mutex_unlock(&cs->event_lock);
}

static int cs_m68_event_thread(void *data)
{
	struct cs_m68 *cs = data;
	int ret;

	for (;;) {
		ret = wait_event_interruptible(cs->event_wait,
			atomic_read(&cs->event_pending) || kthread_should_stop());
		if (kthread_should_stop())
			break;
		if (ret)
			continue;
		atomic_set(&cs->event_pending, 0);
		cs_m68_report_edge(cs);
	}
	return 0;
}

irqreturn_t cs_m68_interrupt(int irq, void *data)
{
	struct cs_m68 *cs = data;

	atomic_set(&cs->event_pending, 1);
	wake_up_interruptible(&cs->event_wait);
	return IRQ_HANDLED;
}

int cs_m68_start_events(struct cs_m68 *cs)
{
	struct task_struct *thread;

	if (!cs->input || !gpio_is_valid(cs->irq_gpio))
		return -EINVAL;
	init_waitqueue_head(&cs->event_wait);
	mutex_init(&cs->event_lock);
	atomic_set(&cs->event_pending, 0);
	cs->finger_down = false;
	thread = kthread_run(cs_m68_event_thread, cs, "ndt");
	if (IS_ERR(thread))
		return PTR_ERR(thread);
	cs->event_thread = thread;
	return 0;
}

void cs_m68_stop_events(struct cs_m68 *cs)
{
	if (!cs->event_thread)
		return;
	kthread_stop(cs->event_thread);
	cs->event_thread = NULL;
	cs_m68_release_key(cs);
}
