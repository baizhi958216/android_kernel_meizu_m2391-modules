// SPDX-License-Identifier: GPL-2.0-only
#include "msm_vidc_internal.h"
#include "msm_vidc_core.h"
#include "msm_vidc_inst.h"
#include "msm_vidc_driver.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_lifetime.h"

DEFINE_SRCU(vidc_operations);
bool vidc_ready;

static void vidc_core_release(struct kref *ref)
{
	struct msm_vidc_core *core = container_of(ref, struct msm_vidc_core, lifetime);
#ifdef CONFIG_MEDIA_CONTROLLER
	if (core->media_initialized)
		media_device_cleanup(&core->media_dev);
#endif
	put_device(&core->pdev->dev);
	msm_vidc_vmem_free((void **)&core);
}

void vidc_core_put(struct msm_vidc_core *core)
{
	kref_put(&core->lifetime, vidc_core_release);
}

void vidc_publish(struct msm_vidc_core *core)
{
	unsigned long flags;

	spin_lock_irqsave(&core->async_lock, flags);
	if (!core->stopping)
		smp_store_release(&vidc_ready, true);
	spin_unlock_irqrestore(&core->async_lock, flags);
}

static void vidc_mark_instances(struct msm_vidc_core *core)
{
	struct msm_vidc_inst *inst;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (!inst->removal_pin && kref_get_unless_zero(&inst->kref)) {
			inst->removal_pin = true;
			smp_store_release(&inst->detached, true);
			wake_up_all(&inst->event_handler.wait);
			wake_up_all(&inst->lifetime_wait);
		}
	}
	list_for_each_entry(inst, &core->dangling_instances, list) {
		if (!inst->removal_pin && kref_get_unless_zero(&inst->kref)) {
			inst->removal_pin = true;
			smp_store_release(&inst->detached, true);
			wake_up_all(&inst->event_handler.wait);
			wake_up_all(&inst->lifetime_wait);
		}
	}
	mutex_unlock(&core->lock);
}

void vidc_stop(struct msm_vidc_core *core)
{
	unsigned long flags;

	WRITE_ONCE(vidc_ready, false);
	spin_lock_irqsave(&core->async_lock, flags);
	core->stopping = true;
	spin_unlock_irqrestore(&core->async_lock, flags);
	vidc_mark_instances(core);
	synchronize_srcu(&vidc_operations);
	/* An admitted open may have published its session after the first scan.
	 * No more opens can enter; a second scan also serializes their close.
	 */
	vidc_mark_instances(core);
	synchronize_srcu(&vidc_operations);
}

static int vidc_poll_bridge_wake(wait_queue_entry_t *entry, unsigned int mode,
	int flags, void *key)
{
	struct vidc_poll_bridge *bridge = container_of(entry, struct vidc_poll_bridge, entry);
	wake_up_all(&bridge->inst->lifetime_wait);
	return 1;
}

void vidc_poll_attach(struct msm_vidc_inst *inst)
{
	int i;

	inst->poll_bridge[0].source = &inst->event_handler.wait;
	for (i = 0; i < 4; i++)
		inst->poll_bridge[i + 1].source = &inst->bufq[i].vb2q->done_wq;
	for (i = 0; i < ARRAY_SIZE(inst->poll_bridge); i++) {
		struct vidc_poll_bridge *bridge = &inst->poll_bridge[i];
		bridge->inst = inst;
		init_waitqueue_func_entry(&bridge->entry, vidc_poll_bridge_wake);
		add_wait_queue(bridge->source, &bridge->entry);
	}
}

void vidc_poll_detach(struct msm_vidc_inst *inst)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(inst->poll_bridge); i++) {
		struct vidc_poll_bridge *bridge = &inst->poll_bridge[i];
		if (bridge->source) {
			remove_wait_queue(bridge->source, &bridge->entry);
			bridge->source = NULL;
		}
	}
}

bool vidc_queue_delayed(struct msm_vidc_core *core, struct workqueue_struct *queue,
	struct delayed_work *work, unsigned long delay)
{
	unsigned long flags;
	bool queued = false;

	spin_lock_irqsave(&core->async_lock, flags);
	if (!core->stopping)
		queued = mod_delayed_work(queue, work, delay);
	spin_unlock_irqrestore(&core->async_lock, flags);
	return queued;
}

bool vidc_queue_work(struct msm_vidc_core *core, struct work_struct *work)
{
	unsigned long flags;
	bool queued = false;

	spin_lock_irqsave(&core->async_lock, flags);
	if (!core->stopping)
		queued = schedule_work(work);
	spin_unlock_irqrestore(&core->async_lock, flags);
	return queued;
}

void vidc_debugfs_drain(struct msm_vidc_core *core)
{
	struct msm_vidc_inst *inst, *found;

	for (;;) {
		found = NULL;
		mutex_lock(&core->lock);
		list_for_each_entry(inst, &core->instances, list) {
			if (inst->debugfs_root && kref_get_unless_zero(&inst->kref)) { found = inst; break; }
		}
		if (!found)
			list_for_each_entry(inst, &core->dangling_instances, list) {
				if (inst->debugfs_root && kref_get_unless_zero(&inst->kref)) { found = inst; break; }
			}
		mutex_unlock(&core->lock);
		if (!found)
			break;
		msm_vidc_debugfs_deinit_inst(found);
		put_inst(found);
	}
	debugfs_remove_recursive(core->debugfs_parent);
	core->debugfs_parent = NULL;
	core->debugfs_root = NULL;
}

bool vidc_cleanup_ready(struct msm_vidc_core *core)
{
	struct msm_vidc_inst *inst;
	bool ready;

	mutex_lock(&core->lock);
	ready = list_empty(&core->instances) && list_empty(&core->dangling_instances);
	list_for_each_entry(inst, &core->instances, list)
		if (inst->removal_pin) { ready = true; break; }
	list_for_each_entry(inst, &core->dangling_instances, list)
		if (inst->removal_pin) { ready = true; break; }
	mutex_unlock(&core->lock);
	return ready;
}
