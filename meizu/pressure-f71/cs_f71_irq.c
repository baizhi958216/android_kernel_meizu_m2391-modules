// SPDX-License-Identifier: GPL-2.0-only
/* Stock masks both GPIO interrupts while its worker samples both keys.
 * Independent source tracks mask reasons to avoid firmware/panel races.
 */
#include <linux/interrupt.h>
#include <linux/lockdep.h>

#include "cs_f71.h"

static void cs_f71_irq_apply(struct cs_f71 *cs)
{
	bool enable = !cs->irq_blocks && !READ_ONCE(cs->removed);
	unsigned int i;

	lockdep_assert_held(&cs->irq_lock);
	if (enable == cs->irq_enabled)
		return;
	for (i = 0; i < ARRAY_SIZE(cs->irq); i++) {
		if (!cs->irq_requested[i])
			continue;
		if (enable)
			enable_irq(cs->irq[i]);
		else
			disable_irq_nosync(cs->irq[i]);
	}
	cs->irq_enabled = enable;
}

/* Never called while command_lock is held: an event worker may need it. */
void cs_f71_irq_block(struct cs_f71 *cs, unsigned long reason)
{
	unsigned int i;

	mutex_lock(&cs->irq_lock);
	cs->irq_blocks |= reason;
	cs_f71_irq_apply(cs);
	mutex_unlock(&cs->irq_lock);
	for (i = 0; i < ARRAY_SIZE(cs->irq); i++)
		if (cs->irq_requested[i])
			synchronize_irq(cs->irq[i]);
	cancel_work_sync(&cs->event_work);
	/* A cancelled pending worker cannot clear its own temporary reason. */
	mutex_lock(&cs->irq_lock);
	cs->irq_blocks &= ~CS_F71_IRQ_EVENT;
	mutex_unlock(&cs->irq_lock);
}

void cs_f71_irq_unblock(struct cs_f71 *cs, unsigned long reason)
{
	mutex_lock(&cs->irq_lock);
	cs->irq_blocks &= ~reason;
	cs_f71_irq_apply(cs);
	mutex_unlock(&cs->irq_lock);
}

static void cs_f71_event_work(struct work_struct *work)
{
	struct cs_f71 *cs = container_of(work, struct cs_f71, event_work);

	mutex_lock(&cs->command_lock);
	if (!READ_ONCE(cs->removed))
		cs_f71_report_keys_locked(cs);
	mutex_unlock(&cs->command_lock);
	cs_f71_irq_unblock(cs, CS_F71_IRQ_EVENT);
}

static irqreturn_t cs_f71_irq_thread(int irq, void *data)
{
	struct cs_f71 *cs = data;

	mutex_lock(&cs->irq_lock);
	if (!cs->irq_blocks && !READ_ONCE(cs->removed)) {
		cs->irq_blocks |= CS_F71_IRQ_EVENT;
		cs_f71_irq_apply(cs);
		schedule_work(&cs->event_work);
	}
	mutex_unlock(&cs->irq_lock);
	return IRQ_HANDLED;
}

void cs_f71_irq_exit(struct cs_f71 *cs)
{
	unsigned int i;

	cs_f71_irq_block(cs, CS_F71_IRQ_REMOVE);
	for (i = 0; i < ARRAY_SIZE(cs->irq); i++) {
		if (!cs->irq_requested[i])
			continue;
		free_irq(cs->irq[i], cs);
		cs->irq_requested[i] = false;
	}
}

int cs_f71_irq_init(struct cs_f71 *cs)
{
	static const char * const names[] = { "ndt_top_irq", "ndt_bottom_irq" };
	unsigned int i;
	int ret;

	mutex_init(&cs->irq_lock);
	INIT_WORK(&cs->event_work, cs_f71_event_work);
	cs->irq_blocks = CS_F71_IRQ_SETUP;
	for (i = 0; i < ARRAY_SIZE(cs->irq); i++) {
		cs->irq[i] = gpiod_to_irq(cs->irq_gpio[i]);
		if (cs->irq[i] < 0) {
			ret = cs->irq[i];
			goto fail;
		}
		/* Publish only after both handlers and their entire state exist. */
		ret = request_threaded_irq(cs->irq[i], NULL, cs_f71_irq_thread,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT | IRQF_NO_AUTOEN, names[i], cs);
		if (ret)
			goto fail;
		cs->irq_requested[i] = true;
	}
	return 0;
fail:
	cs_f71_irq_exit(cs);
	return ret;
}
