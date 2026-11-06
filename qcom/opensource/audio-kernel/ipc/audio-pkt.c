// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>
#include <dsp/msm_audio_ion.h>
#include <dsp/spf-core.h>
#include <ipc/gpr-lite.h>

#define APM_CMD_SHARED_MEM_MAP_REGIONS		0x0100100C
#define APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE	BIT(2)
#define AUDPKT_DRIVER_NAME			"aud_pasthru_adsp"
#define MAX_PACKET_SIZE				4096

static int debug_mask;
module_param(debug_mask, int, 0664);

struct audio_port {
	struct device dev;
	struct cdev cdev;
	struct mutex lock;
	struct gpr_device *endpoint;
	struct sk_buff_head queue;
	wait_queue_head_t readq;
	unsigned int users;
	bool online;
};

struct audio_region {
	u32 addr_lsw;
	u32 addr_msw;
	u32 size;
};

struct audio_region_map {
	u16 mem_pool_id;
	u16 num_regions;
	u32 flags;
	struct audio_region regions[];
};

static DEFINE_MUTEX(binding_lock);
static struct audio_port *active_port;
static struct class audio_pkt_class = { .name = AUDPKT_DRIVER_NAME };

static int audio_pkt_open(struct inode *inode, struct file *file)
{
	struct audio_port *port = container_of(inode->i_cdev, struct audio_port, cdev);
	int ret = -ENODEV;

	mutex_lock(&port->lock);
	if (port->online) {
		get_device(&port->dev);
		port->users++;
		file->private_data = port;
		ret = 0;
	}
	mutex_unlock(&port->lock);
	return ret;
}

static int audio_pkt_release(struct inode *inode, struct file *file)
{
	struct audio_port *port = file->private_data;

	mutex_lock(&port->lock);
	if (!--port->users) {
		if (port->online && port->endpoint) {
			spf_core_apm_close_all();
			msm_audio_ion_crash_handler();
		}
		skb_queue_purge(&port->queue);
	}
	mutex_unlock(&port->lock);
	put_device(&port->dev);
	return 0;
}

static ssize_t audio_pkt_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct audio_port *port = file->private_data;
	struct sk_buff *skb;
	ssize_t len;
	int ret;

	if (!count)
		return 0;
	for (;;) {
		if (!READ_ONCE(port->endpoint))
			return -ENETRESET;
		skb = skb_dequeue(&port->queue);
		if (skb)
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(port->readq, !skb_queue_empty_lockless(&port->queue) ||
					       !READ_ONCE(port->endpoint));
		if (ret)
			return ret;
	}
	len = min_t(size_t, count, skb->len);
	if (copy_to_user(buf, skb->data, len))
		len = -EFAULT;
	kfree_skb(skb);
	return len;
}

static int translate_regions(struct gpr_pkt *pkt, size_t hdr_size, size_t packet_size)
{
	struct audio_region_map *map = (void *)pkt + hdr_size;
	struct audio_region *region;
	dma_addr_t addr;
	size_t len;
	int i, ret;

	if (packet_size - hdr_size < sizeof(*map) ||
	    struct_size(map, regions, map->num_regions) > packet_size - hdr_size)
		return -EINVAL;
	if (!(map->flags & APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE))
		return 0;
	for (i = 0; i < map->num_regions; i++) {
		region = &map->regions[i];
		ret = msm_audio_get_phy_addr(region->addr_lsw, &addr, &len);
		if (ret)
			return ret;
		if (region->size > len)
			return -EINVAL;
		region->addr_lsw = lower_32_bits(addr);
		region->addr_msw = upper_32_bits(addr);
	}
	return 0;
}

static ssize_t audio_pkt_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct audio_port *port = file->private_data;
	struct gpr_pkt *pkt;
	size_t packet_size, hdr_size;
	int ret;

	if (count < sizeof(pkt->hdr) || count > MAX_PACKET_SIZE)
		return -EINVAL;
	pkt = memdup_user(buf, count);
	if (IS_ERR(pkt))
		return PTR_ERR(pkt);
	hdr_size = GPR_PKT_GET_HEADER_BYTE_SIZE(pkt->hdr.header);
	packet_size = GPR_PKT_GET_PACKET_BYTE_SIZE(pkt->hdr.header);
	if (hdr_size < sizeof(pkt->hdr) || packet_size < hdr_size || packet_size > count) {
		ret = -EINVAL;
		goto free;
	}
	ret = mutex_lock_interruptible(&port->lock);
	if (ret)
		goto free;
	if (!port->endpoint) {
		ret = -ENETRESET;
		goto unlock;
	}
	if (pkt->hdr.opcode == APM_CMD_SHARED_MEM_MAP_REGIONS) {
		ret = translate_regions(pkt, hdr_size, packet_size);
		if (ret)
			goto unlock;
	}
	ret = gpr_send_pkt(port->endpoint, pkt);
	if (ret == -ECONNRESET)
		ret = -ENETRESET;
	if (debug_mask)
		dev_dbg(&port->dev, "TX opcode %#x length %zu returned %d\n",
			pkt->hdr.opcode, packet_size, ret);
unlock:
	mutex_unlock(&port->lock);
free:
	kfree(pkt);
	return ret < 0 ? ret : count;
}

static __poll_t audio_pkt_poll(struct file *file, poll_table *wait)
{
	struct audio_port *port = file->private_data;

	poll_wait(file, &port->readq, wait);
	if (!READ_ONCE(port->endpoint))
		return EPOLLERR | EPOLLHUP;
	return skb_queue_empty_lockless(&port->queue) ? 0 : EPOLLIN | EPOLLRDNORM;
}

static const struct file_operations audio_pkt_fops = {
	.owner = THIS_MODULE,
	.open = audio_pkt_open,
	.release = audio_pkt_release,
	.read = audio_pkt_read,
	.write = audio_pkt_write,
	.poll = audio_pkt_poll,
};

static int audio_pkt_receive(struct gpr_device *adev, void *data)
{
	struct audio_port *port = dev_get_drvdata(&adev->dev);
	struct gpr_hdr *hdr = data;
	size_t len = GPR_PKT_GET_PACKET_BYTE_SIZE(hdr->header);
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	skb_put_data(skb, data, len);
	skb_queue_tail(&port->queue, skb);
	wake_up_interruptible(&port->readq);
	return 0;
}

static int audio_pkt_probe(struct gpr_device *adev)
{
	struct audio_port *port = READ_ONCE(active_port);

	if (!port)
		return -EPROBE_DEFER;
	mutex_lock(&port->lock);
	dev_set_drvdata(&adev->dev, port);
	WRITE_ONCE(port->endpoint, adev);
	mutex_unlock(&port->lock);
	wake_up_interruptible(&port->readq);
	return 0;
}

static int audio_pkt_remove(struct gpr_device *adev)
{
	struct audio_port *port = dev_get_drvdata(&adev->dev);

	mutex_lock(&port->lock);
	WRITE_ONCE(port->endpoint, NULL);
	skb_queue_purge(&port->queue);
	mutex_unlock(&port->lock);
	wake_up_interruptible(&port->readq);
	msm_audio_ion_crash_handler();
	return 0;
}

static const struct of_device_id audio_pkt_match_table[] = {
	{ .compatible = "qcom,audio-pkt" },
	{}
};
MODULE_DEVICE_TABLE(of, audio_pkt_match_table);

static struct gpr_driver audio_pkt_driver = {
	.probe = audio_pkt_probe,
	.remove = audio_pkt_remove,
	.callback = audio_pkt_receive,
	.driver = {
		.name = "audio-pkt",
		.of_match_table = audio_pkt_match_table,
	},
};

static void audio_port_release(struct device *dev)
{
	struct audio_port *port = container_of(dev, struct audio_port, dev);

	skb_queue_purge(&port->queue);
	kfree(port);
}

static int audio_pkt_platform_probe(struct platform_device *pdev)
{
	struct audio_port *port;
	int ret;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;
	device_initialize(&port->dev);
	port->dev.class = &audio_pkt_class;
	port->dev.parent = &pdev->dev;
	port->dev.release = audio_port_release;
	skb_queue_head_init(&port->queue);
	init_waitqueue_head(&port->readq);
	mutex_init(&port->lock);
	ret = dev_set_name(&port->dev, AUDPKT_DRIVER_NAME);
	if (ret)
		goto put;
	ret = alloc_chrdev_region(&port->dev.devt, 0, 1, AUDPKT_DRIVER_NAME);
	if (ret)
		goto put;

	mutex_lock(&binding_lock);
	if (active_port) {
		ret = -EBUSY;
		goto region;
	}
	WRITE_ONCE(active_port, port);
	ret = gpr_driver_register(&audio_pkt_driver);
	if (ret)
		goto clear;
	cdev_init(&port->cdev, &audio_pkt_fops);
	port->cdev.owner = THIS_MODULE;
	port->online = true;
	ret = cdev_device_add(&port->cdev, &port->dev);
	if (ret) {
		mutex_lock(&port->lock);
		port->online = false;
		mutex_unlock(&port->lock);
		gpr_driver_unregister(&audio_pkt_driver);
		goto clear;
	}
	platform_set_drvdata(pdev, port);
	mutex_unlock(&binding_lock);
	return 0;
clear:
	WRITE_ONCE(active_port, NULL);
region:
	mutex_unlock(&binding_lock);
	unregister_chrdev_region(port->dev.devt, 1);
put:
	put_device(&port->dev);
	return ret;
}

static void audio_pkt_platform_remove(struct platform_device *pdev)
{
	struct audio_port *port = platform_get_drvdata(pdev);

	mutex_lock(&binding_lock);
	mutex_lock(&port->lock);
	port->online = false;
	mutex_unlock(&port->lock);
	cdev_device_del(&port->cdev, &port->dev);
	gpr_driver_unregister(&audio_pkt_driver);
	WRITE_ONCE(active_port, NULL);
	mutex_unlock(&binding_lock);
	unregister_chrdev_region(port->dev.devt, 1);
	put_device(&port->dev);
}

static const struct of_device_id audio_pkt_platform_match[] = {
	{ .compatible = "qcom,audio-pkt-core-platform" },
	{}
};
MODULE_DEVICE_TABLE(of, audio_pkt_platform_match);

static struct platform_driver audio_pkt_platform_driver = {
	.probe = audio_pkt_platform_probe,
	.remove = audio_pkt_platform_remove,
	.driver = {
		.name = "audio-pkt",
		.of_match_table = audio_pkt_platform_match,
	},
};

static int __init audio_pkt_init(void)
{
	int ret;

	ret = class_register(&audio_pkt_class);
	if (ret)
		return ret;
	ret = platform_driver_register(&audio_pkt_platform_driver);
	if (ret)
		class_unregister(&audio_pkt_class);
	return ret;
}
module_init(audio_pkt_init);

static void __exit audio_pkt_exit(void)
{
	platform_driver_unregister(&audio_pkt_platform_driver);
	class_unregister(&audio_pkt_class);
}
module_exit(audio_pkt_exit);
MODULE_DESCRIPTION("Qualcomm AudioReach GPR passthrough");
MODULE_LICENSE("GPL v2");
