// SPDX-License-Identifier: GPL-2.0-only
/* Firmware request lifetime replaces stock's freed asynchronous context. */
#include <linux/firmware.h>
#include <linux/errno.h>

#include "cs_f71.h"

static void cs_f71_firmware_work(struct work_struct *work)
{
	struct cs_f71 *cs = container_of(work, struct cs_f71, firmware_work);
	const struct firmware *firmware;
	bool force;
	int ret;

	mutex_lock(&cs->firmware_lock);
	force = cs->firmware_force;
	mutex_unlock(&cs->firmware_lock);
	ret = request_firmware(&firmware, "cspress_f71fw.nfw", &cs->client->dev);
	if (ret)
		goto done;
	mutex_lock(&cs->operation_lock);
	if (READ_ONCE(cs->removed))
		goto unlock;
	/* Block and drain the event worker before taking its command lock. */
	cs_f71_irq_block(cs, CS_F71_IRQ_FIRMWARE);
	mutex_lock(&cs->command_lock);
	if (!cs->removed) {
		WRITE_ONCE(cs->updating, true);
		ret = cs_f71_update_firmware_locked(cs, firmware->data,
				firmware->size, force);
		WRITE_ONCE(cs->updating, false);
		if (ret)
			dev_err(&cs->client->dev, "firmware update failed: %d\n", ret);
	}
	mutex_unlock(&cs->command_lock);
	/* A blank/unblank received during programming must take effect before
	 * restoring IRQs; panel work also retains the latest requested state.
	 */
	cs_f71_apply_panel_state(cs);
	cs_f71_irq_unblock(cs, CS_F71_IRQ_FIRMWARE);
	/* Stock update completion enables both initially masked IRQs. A real
	 * panel blank remains an independent reason to keep them disabled.
	 */
	cs_f71_irq_unblock(cs, CS_F71_IRQ_SETUP);
unlock:
	mutex_unlock(&cs->operation_lock);
	release_firmware(firmware);
done:
	mutex_lock(&cs->firmware_lock);
	cs->firmware_pending = false;
	mutex_unlock(&cs->firmware_lock);
}

void cs_f71_firmware_work_init(struct cs_f71 *cs)
{
	mutex_init(&cs->firmware_lock);
	INIT_WORK(&cs->firmware_work, cs_f71_firmware_work);
}

int cs_f71_queue_firmware(struct cs_f71 *cs, bool force)
{
	int ret = 0;

	mutex_lock(&cs->firmware_lock);
	if (READ_ONCE(cs->removed))
		ret = -ENODEV;
	else if (cs->firmware_pending)
		ret = -EBUSY;
	else {
		cs->firmware_force = force;
		cs->firmware_pending = true;
		schedule_work(&cs->firmware_work);
	}
	mutex_unlock(&cs->firmware_lock);
	return ret;
}

void cs_f71_firmware_work_exit(struct cs_f71 *cs)
{
	/* Main teardown has already set removed and drained proc producers. */
	cancel_work_sync(&cs->firmware_work);
	mutex_lock(&cs->firmware_lock);
	cs->firmware_pending = false;
	mutex_unlock(&cs->firmware_lock);
}
