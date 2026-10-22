// SPDX-License-Identifier: GPL-2.0-only
/* Public opaque handles outlive platform removal, without retaining HW state. */
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include "msm_cvp_core.h"
#include "msm_cvp_internal.h"
#include "msm_cvp_lifetime.h"
#include "msm_cvp_res_parse.h"
#include "msm_cvp_debug.h"
#include "msm_cvp_common.h"

struct cvp_handle {
	struct list_head list;
	struct kref ref;
	struct rw_semaphore calls;
	struct msm_cvp_inst *inst;
	wait_queue_head_t wait;
	wait_queue_entry_t bridge;
};

static DECLARE_RWSEM(cvp_operations);
static DEFINE_MUTEX(cvp_handles_lock);
static LIST_HEAD(cvp_handles);
static bool cvp_ready, cvp_stopped;
static struct msm_cvp_core *cvp_bound_core;
static DEFINE_SPINLOCK(cvp_async_lock);

bool cvp_lifetime_stopping(void)
{
	return READ_ONCE(cvp_stopped);
}

static void cvp_handle_free(struct kref *ref)
{
	kfree(container_of(ref, struct cvp_handle, ref));
}

/* Never dereference a caller's opaque pointer before finding a live handle. */
static struct cvp_handle *cvp_handle_get(void *opaque)
{
	struct cvp_handle *handle, *found = NULL;

	mutex_lock(&cvp_handles_lock);
	list_for_each_entry(handle, &cvp_handles, list) {
		if (handle == opaque) {
			kref_get(&handle->ref);
			found = handle;
			break;
		}
	}
	mutex_unlock(&cvp_handles_lock);
	return found;
}

static int cvp_bridge_wake(wait_queue_entry_t *entry, unsigned int mode,
			   int flags, void *key)
{
	struct cvp_handle *handle = container_of(entry, struct cvp_handle, bridge);

	wake_up_interruptible(&handle->wait);
	return 0;
}

/* Caller owns the exclusive handle lease, or the global remove writer lease. */
static void cvp_handle_detach(struct cvp_handle *handle)
{
	struct msm_cvp_inst *inst = handle->inst;

	if (!inst)
		return;
	remove_wait_queue(&inst->event_handler.wq, &handle->bridge);
	handle->inst = NULL;
	msm_cvp_core_close(inst);
	wake_up_interruptible(&handle->wait);
}

void *cvp_handle_open(int core_id, int session_type, struct task_struct *task)
{
	struct cvp_handle *handle = NULL;
	struct msm_cvp_inst *inst;

	down_read(&cvp_operations);
	if (!cvp_ready)
		goto out;
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		goto out;
	inst = msm_cvp_core_open(core_id, session_type, task);
	if (!inst) {
		kfree(handle);
		handle = NULL;
		goto out;
	}
	kref_init(&handle->ref);
	init_rwsem(&handle->calls);
	init_waitqueue_head(&handle->wait);
	init_waitqueue_func_entry(&handle->bridge, cvp_bridge_wake);
	handle->inst = inst;
	add_wait_queue(&inst->event_handler.wq, &handle->bridge);
	mutex_lock(&cvp_handles_lock);
	list_add_tail(&handle->list, &cvp_handles);
	mutex_unlock(&cvp_handles_lock);
out:
	up_read(&cvp_operations);
	return handle;
}


int cvp_handle_close(void *instance)
{
	struct cvp_handle *handle;

	/* Closing an already detached handle must work after device removal. */
	down_read(&cvp_operations);
	handle = cvp_handle_get(instance);
	if (!handle)
		goto out;
	down_write(&handle->calls);
	mutex_lock(&cvp_handles_lock);
	if (!list_empty(&handle->list)) {
		list_del_init(&handle->list);
		mutex_unlock(&cvp_handles_lock);
		cvp_handle_detach(handle);
		kref_put(&handle->ref, cvp_handle_free); /* public owner */
	} else {
		mutex_unlock(&cvp_handles_lock);
	}
	up_write(&handle->calls);
	kref_put(&handle->ref, cvp_handle_free); /* this call */
out:
	up_read(&cvp_operations);
	return 0;
}


static struct cvp_handle *cvp_handle_begin(void *opaque)
{
	struct cvp_handle *handle;

	down_read(&cvp_operations);
	if (!cvp_ready)
		goto fail;
	handle = cvp_handle_get(opaque);
	if (!handle)
		goto fail;
	down_read(&handle->calls);
	if (handle->inst)
		return handle;
	up_read(&handle->calls);
	kref_put(&handle->ref, cvp_handle_free);
fail:
	up_read(&cvp_operations);
	return NULL;
}

static void cvp_handle_end(struct cvp_handle *handle)
{
	up_read(&handle->calls);
	kref_put(&handle->ref, cvp_handle_free);
	up_read(&cvp_operations);
}

int cvp_handle_private(void *instance, unsigned int cmd, struct eva_kmd_arg *arg)
{
	struct cvp_handle *handle = cvp_handle_begin(instance);
	int rc;

	if (!handle)
		return -ENODEV;
	rc = msm_cvp_core_private(handle->inst, cmd, arg);
	cvp_handle_end(handle);
	return rc;
}


long cvp_lifetime_ioctl(void *opaque, unsigned int cmd, unsigned long arg)
{
	struct cvp_handle *handle = cvp_handle_begin(opaque);
	long rc;

	if (!handle)
		return -ENODEV;
	rc = cvp_instance_ioctl(handle->inst, cmd, arg);
	cvp_handle_end(handle);
	return rc;
}

unsigned int cvp_lifetime_poll(void *opaque, struct file *file, poll_table *wait)
{
	struct cvp_handle *handle = cvp_handle_get(opaque);
	struct msm_cvp_inst *inst;
	unsigned long flags;
	unsigned int result = 0;

	if (!handle)
		return POLLERR | POLLHUP;
	/* This queue belongs to the handle, never the detached instance. */
	poll_wait(file, &handle->wait, wait);
	down_read(&cvp_operations);
	down_read(&handle->calls);
	inst = handle->inst;
	if (!cvp_ready || !inst) {
		result = POLLERR | POLLHUP;
		goto out;
	}
	spin_lock_irqsave(&inst->event_handler.lock, flags);
	if (inst->event_handler.event == CVP_SSR_EVENT)
		result |= POLLPRI;
	if (inst->event_handler.event == CVP_DUMP_EVENT)
		result |= POLLIN;
	inst->event_handler.event = CVP_NO_EVENT;
	spin_unlock_irqrestore(&inst->event_handler.lock, flags);
out:
	up_read(&handle->calls);
	up_read(&cvp_operations);
	kref_put(&handle->ref, cvp_handle_free);
	return result;
}

int cvp_handle_poll(void *instance, struct file *file, struct poll_table_struct *wait)
{
	struct cvp_handle *handle = cvp_handle_begin(instance);
	int rc;

	if (!handle)
		return -ENODEV;
	/* The original exported kernel poll operation returns zero. */
	rc = msm_cvp_core_poll(handle->inst, file, wait);
	cvp_handle_end(handle);
	return rc;
}


int cvp_handle_suspend(int core_id)
{
	int rc = -ENODEV;

	down_read(&cvp_operations);
	if (cvp_ready)
		rc = msm_cvp_core_suspend(core_id);
	up_read(&cvp_operations);
	return rc;
}


int cvp_lifetime_prepare(struct msm_cvp_core *core)
{
	int rc = 0;

	down_write(&cvp_operations);
	if (cvp_bound_core)
		rc = -EBUSY;
	else {
		cvp_bound_core = core;
		WRITE_ONCE(cvp_stopped, false);
	}
	up_write(&cvp_operations);
	return rc;
}

bool cvp_lifetime_schedule_ssr(struct msm_cvp_core *core,
		enum hal_ssr_trigger_type type)
{
	unsigned long flags;
	bool queued = false;

	spin_lock_irqsave(&cvp_async_lock, flags);
	if (!core->stopped) {
		core->ssr_type = type;
		schedule_work(&core->ssr_work);
		queued = true;
	}
	spin_unlock_irqrestore(&cvp_async_lock, flags);
	return queued;
}

void cvp_lifetime_publish(struct msm_cvp_core *core)
{
	down_write(&cvp_operations);
	WRITE_ONCE(cvp_stopped, false);
	WRITE_ONCE(cvp_ready, true);
	up_write(&cvp_operations);
}

void cvp_lifetime_stop(struct msm_cvp_core *core)
{
	struct msm_cvp_inst *inst;
	unsigned long flags;

	WRITE_ONCE(cvp_stopped, true);
	WRITE_ONCE(cvp_ready, false);
	if (!core)
		return;
	spin_lock_irqsave(&cvp_async_lock, flags);
	WRITE_ONCE(core->stopped, true);
	spin_unlock_irqrestore(&cvp_async_lock, flags);
	/* All queue heads are initialized before an instance enters this list. */
	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		wake_up_all(&inst->session_queue.wq);
		wake_up_all(&inst->session_queue_fence.wq);
		wake_up_all(&inst->event_handler.wq);
	}
	mutex_unlock(&core->lock);
}

void cvp_lifetime_drain(void)
{
	down_write(&cvp_operations);
}

void cvp_lifetime_debugfs_drain(struct msm_cvp_core *core)
{
	struct msm_cvp_inst *inst, *found;

	/* Public calls and DSP production are already drained by our caller. */
	for (;;) {
		found = NULL;
		mutex_lock(&core->lock);
		list_for_each_entry(inst, &core->instances, list) {
			if (inst->debugfs_root && kref_get_unless_zero(&inst->kref)) {
				found = inst;
				break;
			}
		}
		mutex_unlock(&core->lock);
		if (!found)
			break;
		msm_cvp_debugfs_deinit_inst(found);
		cvp_put_inst(found);
	}
}

void cvp_lifetime_detach(void)
{
	struct cvp_handle *handle;

	/* Writer lease excludes every public call, including close. */
	list_for_each_entry(handle, &cvp_handles, list)
		cvp_handle_detach(handle);
}

void cvp_lifetime_unpublish(void)
{
	cvp_bound_core = NULL;
	up_write(&cvp_operations);
}

int cvp_handle_load_u32_table(struct platform_device *pdev,
		struct device_node *of_node, char *table_name, int struct_size,
		u32 **table, u32 *num_elements)
{
	int rc = -ENODEV;

	down_read(&cvp_operations);
	if (cvp_ready)
		rc = msm_cvp_core_load_u32_table(pdev, of_node, table_name,
			struct_size, table, num_elements);
	up_read(&cvp_operations);
	return rc;
}

