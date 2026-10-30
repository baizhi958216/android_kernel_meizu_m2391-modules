// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sync_file.h>
#include <linux/soc/qcom/qcom_sync_file.h>
#include <uapi/sync_fence/qcom_sync_file.h>

#define DRV_NAME "spec_sync"
#define FENCE_MAX 32

/* The published dma_fence_array owns permanent proxy children. A sync_file
 * may iterate that array or start waiting on it before the bind ioctl.
 */
struct spec_slot {
	struct dma_fence base;
	spinlock_t lock;
	struct dma_fence *input;
	struct dma_fence_cb callback;
	struct dma_fence_array *bound_inputs;
};

struct pending_array {
	struct list_head node;
	struct dma_fence_array *array;
	bool signal_any;
};

static DEFINE_MUTEX(bind_lock);
static LIST_HEAD(pending);
static DECLARE_WAIT_QUEUE_HEAD(bind_wait);
static unsigned int open_count;
static dev_t devno;
static struct cdev cdev;
static struct class *sync_class;

static const char *slot_name(struct dma_fence *fence)
{
	return DRV_NAME;
}

static void slot_release(struct dma_fence *fence)
{
	struct spec_slot *slot = container_of(fence, struct spec_slot, base);

	dma_fence_put(slot->input);
	if (slot->bound_inputs)
		dma_fence_put(&slot->bound_inputs->base);
	dma_fence_free(fence);
	module_put(THIS_MODULE);
}

static const struct dma_fence_ops slot_ops = {
	.get_driver_name = slot_name,
	.get_timeline_name = slot_name,
	.release = slot_release,
};

static void slot_complete(struct dma_fence *input, struct dma_fence_cb *callback)
{
	struct spec_slot *slot = container_of(callback, struct spec_slot, callback);

	if (input->error < 0)
		dma_fence_set_error(&slot->base, input->error);
	dma_fence_signal(&slot->base);
	dma_fence_put(&slot->base);
}

static struct dma_fence *new_slot(void)
{
	struct spec_slot *slot = kzalloc(sizeof(*slot), GFP_KERNEL);

	if (!slot)
		return NULL;
	__module_get(THIS_MODULE);
	spin_lock_init(&slot->lock);
	dma_fence_init(&slot->base, &slot_ops, &slot->lock, dma_fence_context_alloc(1), 1);
	return &slot->base;
}

static struct spec_slot *first_slot(struct dma_fence_array *array)
{
	if (!array || !test_bit(SPEC_FENCE_FLAG_FENCE_ARRAY, &array->base.flags) ||
	    !array->num_fences || array->fences[0]->ops != &slot_ops)
		return NULL;
	return container_of(array->fences[0], struct spec_slot, base);
}

int spec_sync_wait_bind_array(struct dma_fence_array *array, u32 timeout_ms)
{
	struct spec_slot *slot = first_slot(array);

	if (!slot)
		return -EINVAL;
	if (!wait_event_timeout(bind_wait,
			test_bit_acquire(SPEC_FENCE_FLAG_FENCE_ARRAY_BOUND, &array->base.flags),
			msecs_to_jiffies(timeout_ms)))
		return -ETIMEDOUT;
	return slot->input ? 0 : -ECANCELED;
}
EXPORT_SYMBOL_GPL(spec_sync_wait_bind_array);

/* Borrowed until the last reference to the original array is put. */
struct dma_fence_array *spec_sync_bound_inputs(struct dma_fence_array *array)
{
	struct spec_slot *slot = first_slot(array);

	if (!slot || !test_bit_acquire(SPEC_FENCE_FLAG_FENCE_ARRAY_BOUND, &array->base.flags))
		return NULL;
	return slot->bound_inputs;
}
EXPORT_SYMBOL_GPL(spec_sync_bound_inputs);

static int create_array(void __user *arg)
{
	struct fence_create_data data;
	struct dma_fence **slots;
	struct pending_array *entry;
	struct sync_file *file;
	int fd, i, ret;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;
	if (!data.num_fences || data.num_fences > FENCE_MAX)
		return -ERANGE;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	ret = -ENOMEM;
	slots = kcalloc(data.num_fences, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		goto free_entry;
	for (i = 0; i < data.num_fences; i++) {
		slots[i] = new_slot();
		if (!slots[i])
			goto put_slots;
	}
	entry->signal_any = !(data.flags & SPEC_FENCE_SIGNAL_ALL);
	entry->array = dma_fence_array_create(i, slots, dma_fence_context_alloc(1), 1,
					      entry->signal_any);
	if (!entry->array)
		goto put_slots;
	set_bit(SPEC_FENCE_FLAG_FENCE_ARRAY, &entry->array->base.flags);
	file = sync_file_create(&entry->array->base);
	if (!file)
		goto put_array;
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto put_file;
	}
	data.out_bind_fd = fd;
	if (copy_to_user(arg, &data, sizeof(data))) {
		put_unused_fd(fd);
		ret = -EFAULT;
		goto put_file;
	}
	mutex_lock(&bind_lock);
	list_add_tail(&entry->node, &pending);
	fd_install(fd, file->file);
	mutex_unlock(&bind_lock);
	return 0;

put_file:
	fput(file->file);
put_array:
	dma_fence_put(&entry->array->base);
	goto free_entry;
put_slots:
	while (i--)
		dma_fence_put(slots[i]);
	kfree(slots);
free_entry:
	kfree(entry);
	return ret;
}

static bool contains_spec_slot(struct dma_fence *input)
{
	struct dma_fence_unwrap cursor;
	struct dma_fence *leaf;

	dma_fence_unwrap_for_each(leaf, &cursor, input) {
		if (leaf->ops == &slot_ops) {
			dma_fence_put(cursor.chain);
			return true;
		}
	}
	return false;
}

static int bind_inputs(struct pending_array *entry, u64 user_fds)
{
	struct dma_fence *inputs[FENCE_MAX];
	int fds[FENCE_MAX];
	struct dma_fence_array *array = entry->array;
	struct spec_slot *first = first_slot(array);
	bool flat = !entry->signal_any || array->num_fences == 1;
	int i, acquired = 0, ret = -EINVAL;

	if (copy_from_user(fds, u64_to_user_ptr(user_fds), array->num_fences * sizeof(*fds)))
		return -EFAULT;
	for (i = 0; i < array->num_fences; i++) {
		inputs[i] = sync_file_get_fence(fds[i]);
		if (!inputs[i])
			goto put_inputs;
		acquired++;
		if (contains_spec_slot(inputs[i]))
			goto put_inputs;
		if (dma_fence_is_container(inputs[i]))
			flat = false;
	}

	/* The display HW joins a flat set of producers and waits for all of
	 * them. Other shapes retain their exact semantics through the proxies.
	 */
	if (flat) {
		struct dma_fence **leaves = kmemdup(inputs, acquired * sizeof(*inputs), GFP_KERNEL);

		if (!leaves) {
			ret = -ENOMEM;
			goto put_inputs;
		}
		first->bound_inputs = dma_fence_array_create(acquired, leaves,
						dma_fence_context_alloc(1), 1, false);
		if (!first->bound_inputs) {
			kfree(leaves);
			ret = -ENOMEM;
			goto put_inputs;
		}
		for (i = 0; i < acquired; i++)
			dma_fence_get(inputs[i]);
	}
	for (i = 0; i < acquired; i++) {
		struct spec_slot *slot = container_of(array->fences[i], struct spec_slot, base);

		slot->input = inputs[i];
		dma_fence_get(&slot->base);
		if (dma_fence_add_callback(inputs[i], &slot->callback, slot_complete))
			slot_complete(inputs[i], &slot->callback);
	}
	smp_mb__before_atomic();
	set_bit(SPEC_FENCE_FLAG_FENCE_ARRAY_BOUND, &array->base.flags);
	wake_up_all(&bind_wait);
	return 0;

put_inputs:
	while (acquired--)
		dma_fence_put(inputs[acquired]);
	return ret;
}

static int bind_array(void __user *arg)
{
	struct fence_bind_data data;
	struct pending_array *entry;
	struct dma_fence *fence;
	int ret = -EINVAL;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;
	fence = sync_file_get_fence(data.out_bind_fd);
	if (!fence)
		return -EINVAL;
	mutex_lock(&bind_lock);
	list_for_each_entry(entry, &pending, node) {
		if (&entry->array->base != fence)
			continue;
		ret = bind_inputs(entry, data.fds);
		if (!ret) {
			list_del(&entry->node);
			dma_fence_put(fence);
			kfree(entry);
		}
		break;
	}
	mutex_unlock(&bind_lock);
	dma_fence_put(fence);
	return ret;
}

static int spec_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	mutex_lock(&bind_lock);
	if (open_count >= (IS_ENABLED(CONFIG_DEBUG_FS) ? 2 : 1))
		ret = -EEXIST;
	else
		open_count++;
	mutex_unlock(&bind_lock);
	return ret;
}

static int spec_release(struct inode *inode, struct file *file)
{
	struct pending_array *entry, *next;
	int i;

	mutex_lock(&bind_lock);
	if (!--open_count) {
		list_for_each_entry_safe(entry, next, &pending, node) {
			for (i = 0; i < entry->array->num_fences; i++) {
				struct dma_fence *slot = entry->array->fences[i];

				dma_fence_set_error(slot, -ECANCELED);
				dma_fence_signal(slot);
			}
			set_bit(SPEC_FENCE_FLAG_FENCE_ARRAY_BOUND, &entry->array->base.flags);
			list_del(&entry->node);
			dma_fence_put(&entry->array->base);
			kfree(entry);
		}
		wake_up_all(&bind_wait);
	}
	mutex_unlock(&bind_lock);
	return 0;
}

static long spec_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *ptr = (void __user *)arg;

	switch (cmd) {
	case SPEC_SYNC_IOC_CREATE_FENCE:
		return create_array(ptr);
	case SPEC_SYNC_IOC_BIND:
		return bind_array(ptr);
	case SPEC_SYNC_IOC_GET_VER:
		return put_user(1, (u32 __user *)ptr);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations spec_fops = {
	.owner = THIS_MODULE,
	.open = spec_open,
	.release = spec_release,
	.unlocked_ioctl = spec_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int __init spec_init(void)
{
	struct device *dev;
	int ret;

	sync_class = class_create("sync");
	if (IS_ERR(sync_class))
		return PTR_ERR(sync_class);
	ret = alloc_chrdev_region(&devno, 0, 1, DRV_NAME);
	if (ret)
		goto destroy_class;
	cdev_init(&cdev, &spec_fops);
	cdev.owner = THIS_MODULE;
	ret = cdev_add(&cdev, devno, 1);
	if (ret)
		goto unregister;
	dev = device_create(sync_class, NULL, devno, NULL, DRV_NAME);
	if (!IS_ERR(dev))
		return 0;
	ret = PTR_ERR(dev);
	cdev_del(&cdev);
unregister:
	unregister_chrdev_region(devno, 1);
destroy_class:
	class_destroy(sync_class);
	return ret;
}

static void __exit spec_exit(void)
{
	device_destroy(sync_class, devno);
	cdev_del(&cdev);
	unregister_chrdev_region(devno, 1);
	class_destroy(sync_class);
}

module_init(spec_init);
module_exit(spec_exit);

MODULE_DESCRIPTION("QCOM Speculative Sync Driver");
MODULE_LICENSE("GPL v2");
