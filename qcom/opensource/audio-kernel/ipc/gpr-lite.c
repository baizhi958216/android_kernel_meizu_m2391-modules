/* Copyright (c) 2011-2017, 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2018, Linaro Limited
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <ipc/gpr-lite.h>
#include <linux/rpmsg.h>
#include <linux/of.h>

#include <soc/snd_event.h>
#include <dsp/audio_notifier.h>

#define APM_EVENT_MODULE_TO_CLIENT	0x03001000
#define WAKELOCK_TIMEOUT 200

struct gpr {
	struct rpmsg_endpoint *ch;
	struct device *dev;
	spinlock_t gpr_lock;
	bool is_initial_boot;

	spinlock_t svcs_lock;
	struct idr svcs_idr;
	int dest_domain_id;
	struct work_struct notifier_reg_work;
	struct notifier_block notifier;
	char *notifier_name;
	int notifier_domain;
	bool registered;
	struct wakeup_source *wsource;
};

static struct gpr_q6 q6;


enum gpr_subsys_state gpr_get_q6_state(void)
{
	return atomic_read(&q6.q6_state);
}
EXPORT_SYMBOL(gpr_get_q6_state);

enum gpr_subsys_state gpr_get_modem_state(void)
{
	return atomic_read(&q6.modem_state);
}
EXPORT_SYMBOL(gpr_get_modem_state);


/**
 * gpr_send_pkt() - Send a gpr message from gpr device
 *
 * @adev: Pointer to previously registered gpr device.
 * @pkt: Pointer to gpr packet to send
 *
 * Return: Will be an negative and/or packet size on success.
 */
int gpr_send_pkt(struct gpr_device *adev, struct gpr_pkt *pkt)
{
	struct gpr *gpr;
	u32 pkt_size;
	int ret = -ENETRESET;

	if (!adev || !pkt)
		return -EINVAL;

	mutex_lock(&adev->tx_lock);
	if (!adev->online)
		goto unlock;
	if (adev->domain_id == GPR_DOMAIN_ADSP && gpr_get_q6_state() != GPR_SUBSYS_LOADED)
		goto unlock;
	if (adev->domain_id == GPR_DOMAIN_MODEM && gpr_get_modem_state() == GPR_SUBSYS_DOWN)
		goto unlock;

	gpr = dev_get_drvdata(adev->dev.parent);
	pkt->hdr.dst_domain_id = adev->domain_id;
	pkt_size = GPR_PKT_GET_PACKET_BYTE_SIZE(pkt->hdr.header);
	ret = rpmsg_trysend(gpr->ch, pkt, pkt_size);
	if (!ret)
		ret = pkt_size;
unlock:
	mutex_unlock(&adev->tx_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gpr_send_pkt);

 /**
  * apr_set_modem_state - Update modem load status.
  *
  * @state: State to update modem load status
  *
 */
void gpr_set_modem_state(enum gpr_subsys_state state)
{
	atomic_set(&q6.modem_state, state);
}
EXPORT_SYMBOL(gpr_set_modem_state);


int gpr_set_q6_state(enum gpr_subsys_state state)
{
	if (state < GPR_SUBSYS_DOWN || state > GPR_SUBSYS_LOADED)
		return -EINVAL;
	atomic_set(&q6.q6_state, state);
	return 0;
}
EXPORT_SYMBOL(gpr_set_q6_state);

static void gpr_ssr_disable(struct device *dev, void *data)
{
	gpr_set_q6_state(GPR_SUBSYS_DOWN);
}

static const struct snd_event_ops gpr_ssr_ops = {
	.disable = gpr_ssr_disable,
};

static int gpr_notifier_service_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct gpr *gpr = container_of(nb, struct gpr, notifier);

	if (event != AUDIO_NOTIFIER_SERVICE_UP && event != AUDIO_NOTIFIER_SERVICE_DOWN)
		return NOTIFY_DONE;
	spin_lock(&gpr->gpr_lock);
	if (event == AUDIO_NOTIFIER_SERVICE_DOWN && gpr->is_initial_boot) {
		spin_unlock(&gpr->gpr_lock);
		return NOTIFY_OK;
	}
	gpr->is_initial_boot = false;
	spin_unlock(&gpr->gpr_lock);

	if (gpr->dest_domain_id == GPR_DOMAIN_MODEM) {
		gpr_set_modem_state(event == AUDIO_NOTIFIER_SERVICE_DOWN ?
				    GPR_SUBSYS_DOWN : GPR_SUBSYS_LOADED);
	} else {
		gpr_set_q6_state(event == AUDIO_NOTIFIER_SERVICE_DOWN ?
				 GPR_SUBSYS_DOWN : GPR_SUBSYS_LOADED);
		snd_event_notify(gpr->dev, event == AUDIO_NOTIFIER_SERVICE_DOWN ?
				 SND_EVENT_DOWN : SND_EVENT_UP);
	}
	return NOTIFY_OK;
}

static void gpr_dev_release(struct device *dev)
{
	struct gpr_device *adev = to_gpr_device(dev);

	of_node_put(dev->of_node);
	kfree(adev);
}

static int gpr_callback(struct rpmsg_device *rpdev, void *buf,
				  int len, void *priv, u32 addr)
{
	struct gpr_hdr *hdr = buf;
	struct gpr_driver *driver;
	struct gpr_device *svc;
	struct gpr *gpr;
	u32 hdr_size;
	int ret = -ENODEV;

	if (len < GPR_HDR_SIZE)
		return -EINVAL;
	hdr_size = GPR_PKT_GET_HEADER_BYTE_SIZE(hdr->header);
	if (hdr_size < GPR_HDR_SIZE || hdr_size > len ||
	    GPR_PKT_GET_PACKET_BYTE_SIZE(hdr->header) != len)
		return -EINVAL;

	/* GLINK calls this from its RX spinlock. Only bound services are
	 * published in the IDR. Removal drains the readers before teardown.
	 */
	rcu_read_lock();
	gpr = rcu_dereference_raw(rpdev->dev.driver_data);
	if (!gpr)
		goto unlock;
	svc = hdr->dst_port <= INT_MAX ? idr_find(&gpr->svcs_idr, hdr->dst_port) : NULL;
	if (!svc)
		svc = idr_find(&gpr->svcs_idr, GPR_SVC_MAX);
	if (!svc)
		goto unlock;
	driver = to_gpr_driver(svc->dev.driver);
	if (hdr->opcode == APM_EVENT_MODULE_TO_CLIENT)
		pm_wakeup_ws_event(gpr->wsource, WAKELOCK_TIMEOUT, true);
	ret = driver->callback(svc, buf);
unlock:
	rcu_read_unlock();
	return ret;
}

static int gpr_device_match(struct device *dev, const struct device_driver *drv)
{
	struct gpr_device *adev = to_gpr_device(dev);
	struct gpr_driver *adrv = to_gpr_driver(drv);
	const struct gpr_device_id *id = adrv->id_table;

	/* Attempt an OF style match first */
	if (of_driver_match_device(dev, drv))
		return 1;

	if (!id)
		return 0;

	while (id->domain_id != 0 || id->svc_id != 0) {
		if (id->domain_id == adev->domain_id &&
		    id->svc_id == adev->svc_id)
			return 1;
		id++;
	}

	return 0;
}

static int gpr_device_probe(struct device *dev)
{
	struct gpr_device *adev = to_gpr_device(dev);
	struct gpr_driver *adrv = to_gpr_driver(dev->driver);

	struct gpr *gpr = dev_get_drvdata(dev->parent);
	int ret;

	/* A client may queue its first TX from probe. Publish its RX route
	 * before allowing that worker to send its first request.
	 */
	mutex_lock(&adev->tx_lock);
	ret = adrv->probe(adev);
	if (ret)
		goto unlock;
	spin_lock_irq(&gpr->svcs_lock);
	ret = idr_alloc(&gpr->svcs_idr, adev, adev->svc_id, adev->svc_id + 1, GFP_ATOMIC);
	spin_unlock_irq(&gpr->svcs_lock);
	if (ret >= 0) {
		adev->online = true;
		ret = 0;
	} else if (adrv->remove) {
		mutex_unlock(&adev->tx_lock);
		adrv->remove(adev);
		return ret;
	}
unlock:
	mutex_unlock(&adev->tx_lock);
	return ret;
}

static void gpr_device_remove(struct device *dev)
{
	struct gpr_device *adev = to_gpr_device(dev);
	struct gpr_driver *adrv = to_gpr_driver(dev->driver);
	struct gpr *gpr = dev_get_drvdata(dev->parent);

	mutex_lock(&adev->tx_lock);
	adev->online = false;
	mutex_unlock(&adev->tx_lock);
	spin_lock_irq(&gpr->svcs_lock);
	idr_remove(&gpr->svcs_idr, adev->svc_id);
	spin_unlock_irq(&gpr->svcs_lock);
	synchronize_rcu();
	if (adrv->remove)
		adrv->remove(adev);
}

static int gpr_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	struct gpr_device *adev = to_gpr_device(dev);
	int ret;

	ret = of_device_uevent_modalias(dev, env);
	if (ret != -ENODEV)
		return ret;

	return add_uevent_var(env, "MODALIAS=gpr:%s", adev->name);
}

struct bus_type gprbus = {
	.name		= "gprbus",
	.match		= gpr_device_match,
	.probe		= gpr_device_probe,
	.uevent		= gpr_uevent,
	.remove		= gpr_device_remove,
};
EXPORT_SYMBOL_GPL(gprbus);

static int gpr_add_device(struct device *dev, struct device_node *np,
			  const struct gpr_device_id *id)
{
	struct gpr_device *adev = NULL;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	mutex_init(&adev->tx_lock);
	device_initialize(&adev->dev);
	adev->dev.release = gpr_dev_release;

	adev->svc_id = id->svc_id;
	adev->domain_id = id->domain_id;
	adev->version = id->svc_version;
	if (np)
		strscpy(adev->name, np->name, GPR_NAME_SIZE);
	else
		strscpy(adev->name, id->name, GPR_NAME_SIZE);

	ret = dev_set_name(&adev->dev, "gprsvc:%s:%x:%x", adev->name, id->domain_id, id->svc_id);
	if (ret) {
		put_device(&adev->dev);
		return ret;
	}

	adev->dev.bus = &gprbus;
	adev->dev.parent = dev;
	adev->dev.of_node = of_node_get(np);

	dev_info_ratelimited(dev, "Adding GPR dev: %s\n", dev_name(&adev->dev));

	ret = device_add(&adev->dev);
	if (ret) {
		dev_err_ratelimited(dev, "device_register failed: %d\n", ret);
		put_device(&adev->dev);
	}

	return ret;
}

static int gpr_register_children(struct gpr *gpr)
{
	for_each_available_child_of_node_scoped(gpr->dev->of_node, node) {
		struct gpr_device_id id = { .domain_id = gpr->dest_domain_id };
		int ret;

		ret = of_property_read_u32(node, "reg", &id.svc_id);
		if (ret)
			return ret;
		ret = gpr_add_device(gpr->dev, node, &id);
		if (ret)
			return ret;
	}
	return 0;
}

static void gpr_notifier_register(struct work_struct *work)
{
	struct gpr *gpr = container_of(work, struct gpr, notifier_reg_work);
	int ret;

	ret = audio_notifier_register(gpr->notifier_name, gpr->notifier_domain, &gpr->notifier);
	if (ret)
		dev_err(gpr->dev, "domain notifier registration failed: %d\n", ret);
	else
		gpr->registered = true;
}

static int gpr_remove_device(struct device *dev, void *unused)
{
	device_unregister(dev);
	return 0;
}

static int gpr_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct gpr *gpr;
	int ret;

	if (!audio_notifier_probe_status())
		return -EPROBE_DEFER;
	gpr = devm_kzalloc(dev, sizeof(*gpr), GFP_KERNEL);
	if (!gpr)
		return -ENOMEM;
	ret = of_property_read_u32(dev->of_node, "reg", &gpr->dest_domain_id);
	if (ret)
		return ret;
	switch (gpr->dest_domain_id) {
	case GPR_DOMAIN_ADSP:
		gpr->notifier_name = "gpr_adsp";
		gpr->notifier_domain = AUDIO_NOTIFIER_ADSP_DOMAIN;
		break;
	case GPR_DOMAIN_MODEM:
		gpr->notifier_name = "gpr_modem";
		gpr->notifier_domain = AUDIO_NOTIFIER_MODEM_DOMAIN;
		break;
	default:
		return -EINVAL;
	}
	gpr->notifier.notifier_call = gpr_notifier_service_cb;
	gpr->ch = rpdev->ept;
	gpr->dev = dev;
	gpr->is_initial_boot = true;
	spin_lock_init(&gpr->gpr_lock);
	spin_lock_init(&gpr->svcs_lock);
	idr_init(&gpr->svcs_idr);
	INIT_WORK(&gpr->notifier_reg_work, gpr_notifier_register);
	gpr->wsource = wakeup_source_register(dev, "audio-gpr");
	if (!gpr->wsource)
		return -ENOMEM;
	ret = snd_event_client_register(dev, &gpr_ssr_ops, NULL);
	if (ret)
		goto wake;
	rcu_assign_pointer(dev->driver_data, gpr);
	ret = gpr_register_children(gpr);
	if (ret) {
		device_for_each_child(dev, NULL, gpr_remove_device);
		RCU_INIT_POINTER(dev->driver_data, NULL);
		synchronize_rcu();
		snd_event_client_deregister(dev);
		idr_destroy(&gpr->svcs_idr);
		goto wake;
	}
	schedule_work(&gpr->notifier_reg_work);
	return 0;
wake:
	wakeup_source_unregister(gpr->wsource);
	return ret;
}

static void gpr_remove(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct gpr *gpr = dev_get_drvdata(dev);

	cancel_work_sync(&gpr->notifier_reg_work);
	if (gpr->registered)
		audio_notifier_deregister(gpr->notifier_name);
	if (gpr->dest_domain_id == GPR_DOMAIN_ADSP)
		gpr_set_q6_state(GPR_SUBSYS_DOWN);
	else
		gpr_set_modem_state(GPR_SUBSYS_DOWN);
	device_for_each_child(dev, NULL, gpr_remove_device);
	RCU_INIT_POINTER(dev->driver_data, NULL);
	synchronize_rcu();
	snd_event_client_deregister(dev);
	wakeup_source_unregister(gpr->wsource);
	idr_destroy(&gpr->svcs_idr);
}

/*
 * __gpr_driver_register() - Client driver registration with gprbus
 *
 * @drv:Client driver to be associated with client-device.
 * @owner: owning module/driver
 *
 * This API will register the client driver with the gprbus
 * It is called from the driver's module-init function.
 */
int __gpr_driver_register(struct gpr_driver *drv, struct module *owner)
{
	drv->driver.bus = &gprbus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__gpr_driver_register);

/*
 * gpr_driver_unregister() - Undo effect of gpr_driver_register
 *
 * @drv: Client driver to be unregistered
 */
void gpr_driver_unregister(struct gpr_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(gpr_driver_unregister);

static const struct of_device_id gpr_of_match[] = {
	{ .compatible = "qcom,gpr"},
	{}
};
MODULE_DEVICE_TABLE(of, gpr_of_match);

static struct rpmsg_driver gpr_driver = {
	.probe = gpr_probe,
	.remove = gpr_remove,
	.callback = gpr_callback,
	.drv = {
		.name = "qcom,gpr",
		.of_match_table = gpr_of_match,
	},
};

static int __init gpr_init(void)
{
	int ret;

	ret = bus_register(&gprbus);
	if (ret)
		return ret;
	ret = register_rpmsg_driver(&gpr_driver);
	if (ret)
		bus_unregister(&gprbus);

	return ret;
}

static void __exit gpr_exit(void)
{
	unregister_rpmsg_driver(&gpr_driver);
	bus_unregister(&gprbus);
}

subsys_initcall(gpr_init);
module_exit(gpr_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI GPR Bus");
