// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/io.h>
#include "msm_cvp_core.h"
#include "msm_cvp_lifetime.h"
#include "msm_cvp_common.h"
#include "msm_cvp_debug.h"
#include "msm_cvp_internal.h"
#include "msm_cvp_res_parse.h"
#include "msm_cvp_resources.h"
#include "cvp_hfi_api.h"
#include "cvp_private.h"
#include "msm_cvp_clocks.h"
#include "msm_cvp_dsp.h"
#include "msm_cvp.h"
#include "vm/cvp_vm.h"

#define CLASS_NAME              "cvp"
#define DRIVER_NAME             "cvp"

struct msm_cvp_drv *cvp_driver;

static int cvp_open(struct inode *inode, struct file *filp)
{
	void *handle = msm_cvp_open(MSM_CORE_CVP, MSM_CVP_USER, current);

	if (!handle)
		return -ENODEV;
	filp->private_data = handle;
	return 0;
}

static int cvp_close(struct inode *inode, struct file *filp)
{
	int rc = msm_cvp_close(filp->private_data);

	filp->private_data = NULL;
	return rc;
}

static unsigned int cvp_poll(struct file *filp, struct poll_table_struct *p)
{
	return cvp_lifetime_poll(filp->private_data, filp, p);
}

static const struct file_operations cvp_fops = {
	.owner = THIS_MODULE,
	.open = cvp_open,
	.release = cvp_close,
	.unlocked_ioctl = cvp_unblocked_ioctl,
	.compat_ioctl = cvp_compat_ioctl,
	.poll = cvp_poll,
};

static int read_platform_resources(struct msm_cvp_core *core,
		struct platform_device *pdev)
{
	int rc = 0;

	if (!core || !pdev) {
		dprintk(CVP_ERR, "%s: Invalid params %pK %pK\n",
			__func__, core, pdev);
		return -EINVAL;
	}

	core->hfi_type = CVP_HFI_IRIS;
	core->resources.pdev = pdev;
	if (pdev->dev.of_node) {
		/* Target supports DT, parse from it */
		rc = cvp_read_platform_resources_from_drv_data(core);
		rc = cvp_read_platform_resources_from_dt(&core->resources);
	} else {
		dprintk(CVP_ERR, "pdev node is NULL\n");
		rc = -EINVAL;
	}
	return rc;
}

static void init_cycle_info(struct cvp_cycle_info *info)
{
	memset(info->sum_fps, 0, HFI_MAX_HW_THREADS*sizeof(u32));
	memset(info->hi_ctrl_lim, 0, HFI_MAX_HW_THREADS*sizeof(u32));
	memset(info->lo_ctrl_lim, 0, HFI_MAX_HW_THREADS*sizeof(u32));
	memset(info->cycle, 0,
		HFI_MAX_HW_THREADS*sizeof(struct cvp_cycle_stat));
	info->conf_freq = 0;
}

static int msm_cvp_initialize_core(struct platform_device *pdev,
				struct msm_cvp_core *core)
{
	int i = 0;
	int rc = 0;

	if (!core)
		return -EINVAL;
	rc = read_platform_resources(core, pdev);
	if (rc) {
		dprintk(CVP_ERR, "Failed to get platform resources\n");
		return rc;
	}

	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->lock);
	mutex_init(&core->clk_lock);
	mutex_init(&core->idr_mtx);
	idr_init(&core->sess_idr);

	core->state = CVP_CORE_UNINIT;
	for (i = SYS_MSG_INDEX(SYS_MSG_START);
		i <= SYS_MSG_INDEX(SYS_MSG_END); i++) {
		init_completion(&core->completions[i]);
	}

	INIT_DELAYED_WORK(&core->fw_unload_work, msm_cvp_fw_unload_handler);
	INIT_WORK(&core->ssr_work, msm_cvp_ssr_handler);
	init_cycle_info(&core->dyn_clk);
	core->ssr_count = 0;

	return rc;
}

static ssize_t link_name_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct msm_cvp_core *core = dev_get_drvdata(dev);

	if (core)
		if (dev == core->dev)
			return snprintf(buf, PAGE_SIZE, "msm_cvp\n");
		else
			return 0;
	else
		return 0;
}

static DEVICE_ATTR_RO(link_name);

static ssize_t pwr_collapse_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long val = 0;
	int rc = 0;
	struct msm_cvp_core *core = NULL;

	rc = kstrtoul(buf, 0, &val);
	if (rc)
		return rc;
	else if (!val)
		return -EINVAL;

	core = get_cvp_core(MSM_CORE_CVP);
	if (!core)
		return -EINVAL;
	core->resources.msm_cvp_pwr_collapse_delay = val;
	return count;
}

static ssize_t pwr_collapse_delay_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct msm_cvp_core *core = NULL;

	core = get_cvp_core(MSM_CORE_CVP);
	if (!core)
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%u\n",
		core->resources.msm_cvp_pwr_collapse_delay);
}

static DEVICE_ATTR_RW(pwr_collapse_delay);

static ssize_t thermal_level_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", cvp_driver->thermal_level);
}

static ssize_t thermal_level_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int rc = 0, val = 0;

	rc = kstrtoint(buf, 0, &val);
	if (rc || val < 0) {
		dprintk(CVP_WARN,
			"Invalid thermal level value: %s\n", buf);
		return -EINVAL;
	}
	dprintk(CVP_PWR, "Thermal level old %d new %d\n",
			cvp_driver->thermal_level, val);

	if (val == cvp_driver->thermal_level)
		return count;
	cvp_driver->thermal_level = val;

	msm_cvp_comm_handle_thermal_event();
	return count;
}

static DEVICE_ATTR_RW(thermal_level);

static ssize_t sku_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d",
			cvp_driver->sku_version);
}

static DEVICE_ATTR_RO(sku_version);

static ssize_t boot_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int rc = 0, val = 0;
	static int booted;

	rc = kstrtoint(buf, 0, &val);
	if (rc || val < 0) {
		dprintk(CVP_WARN,
			"Invalid boot value: %s\n", buf);
		return -EINVAL;
	}

	if (val > 0 && booted == 0) {
		struct msm_cvp_inst *inst;

		inst = msm_cvp_core_open(MSM_CORE_CVP, MSM_CVP_BOOT, current);
		if (!inst) {
			dprintk(CVP_ERR,
			"Failed to create cvp instance\n");
			return -ENOMEM;
		}
		rc = msm_cvp_core_close(inst);
		if (rc) {
			dprintk(CVP_ERR,
			"Failed to close cvp instance\n");
			return rc;
		}
	} else if ((val == 2) && booted) {
		struct msm_cvp_inst *inst;

		inst = msm_cvp_core_open(MSM_CORE_CVP, MSM_CVP_USER, current);
		if (!inst) {
			dprintk(CVP_ERR,
			"Failed to create eva instance\n");
			return -ENOMEM;
		}
		rc = msm_cvp_session_create(inst);
		if (rc)
			dprintk(CVP_ERR, "Failed to create eva session\n");

		rc = msm_cvp_core_close(inst);
		if (rc) {
			dprintk(CVP_ERR,
			"Failed to close eva instance\n");
			return rc;
		}
	}
	booted = 1;
	return count;
}

static DEVICE_ATTR_WO(boot);

static struct attribute *msm_cvp_core_attrs[] = {
		&dev_attr_pwr_collapse_delay.attr,
		&dev_attr_thermal_level.attr,
		&dev_attr_sku_version.attr,
		&dev_attr_link_name.attr,
		&dev_attr_boot.attr,
		NULL
};

static struct attribute_group msm_cvp_core_attr_group = {
		.attrs = msm_cvp_core_attrs,
};

static const struct of_device_id msm_cvp_plat_match[] = {
	{.compatible = "qcom,msm-cvp"},
	{.compatible = "qcom,msm-cvp,context-bank"},
	{.compatible = "qcom,msm-cvp,bus"},
	{.compatible = "qcom,msm-cvp,mem-cdsp"},
	{}
};

/* Keep all hardware alive until public operations and asynchronous producers drain. */
static void cvp_stop_core(struct msm_cvp_core *core)
{
	mutex_lock(&core->stop_lock);
	if (core->stopped)
		goto out;
	cvp_lifetime_stop(core->initialized ? core : NULL);
	core->stopped = true;
	cvp_dsp_prepare_stop();
	if (core->public_cdev) {
		cdev_del(core->public_cdev);
		core->public_cdev = NULL;
	}
	if (core->sysfs_registered) {
		sysfs_remove_group(&core->dev->kobj, &msm_cvp_core_attr_group);
		core->sysfs_registered = false;
	}
	cvp_lifetime_drain();
	if (core->initialized) {
		cancel_work_sync(&core->ssr_work);
		cancel_delayed_work_sync(&core->fw_unload_work);
	}
	cvp_dsp_device_stop();
	if (core->initialized)
		cvp_lifetime_debugfs_drain(core);
	debugfs_remove_recursive(core->driver_debugfs_root);
	if (cvp_driver->debugfs_root == core->driver_debugfs_root)
		cvp_driver->debugfs_root = NULL;
	core->driver_debugfs_root = NULL;
	core->debugfs_root = NULL;
	cvp_lifetime_detach();
	if (core->initialized) {
		cancel_work_sync(&core->ssr_work);
		cancel_delayed_work_sync(&core->fw_unload_work);
	}
	if (core->device && !IS_ERR(core->device)) {
		cvp_hfi_deinitialize(core->hfi_type, core->device);
		core->device = NULL;
	}
	cvp_dsp_device_exit();
	if (core->listed) {
		mutex_lock(&cvp_driver->lock);
		list_del_init(&core->list);
		cvp_driver->num_cores--;
		mutex_unlock(&cvp_driver->lock);
		core->listed = false;
	}
	if (core->dev && !IS_ERR(core->dev)) {
		dev_set_drvdata(core->dev, NULL);
		device_destroy(core->class, core->dev_num);
		core->dev = NULL;
	}
	if (core->class && !IS_ERR(core->class)) {
		class_destroy(core->class);
		core->class = NULL;
	}
	if (core->devnum_registered) {
		unregister_chrdev_region(core->dev_num, 1);
		core->devnum_registered = false;
	}
	msm_cvp_free_platform_resources(&core->resources);
	cvp_lifetime_unpublish();
out:
	mutex_unlock(&core->stop_lock);
}

static void cvp_free_core(struct msm_cvp_core *core)
{
	if (core->initialized) {
		idr_destroy(&core->sess_idr);
		mutex_destroy(&core->idr_mtx);
		mutex_destroy(&core->lock);
		mutex_destroy(&core->clk_lock);
	}
	mutex_destroy(&core->stop_lock);
	kfree(core);
}

static int msm_probe_cvp_device(struct platform_device *pdev)
{
	struct msm_cvp_core *core;
	int rc;

	if (!cvp_driver)
		return -EINVAL;
	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	mutex_init(&core->stop_lock);
	rc = cvp_lifetime_prepare(core);
	if (rc) {
		cvp_free_core(core);
		return rc;
	}
	core->platform_data = cvp_get_drv_data(&pdev->dev);
	dev_set_drvdata(&pdev->dev, core);
	rc = msm_cvp_initialize_core(pdev, core);
	if (rc)
		goto fail;
	core->initialized = true;
	core->id = MSM_CORE_CVP;
	rc = alloc_chrdev_region(&core->dev_num, 0, 1, DRIVER_NAME);
	if (rc)
		goto fail;
	core->devnum_registered = true;
	core->class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(core->class)) {
		rc = PTR_ERR(core->class);
		goto fail;
	}
	core->dev = device_create(core->class, NULL, core->dev_num,
				  core, DRIVER_NAME);
	if (IS_ERR(core->dev)) {
		rc = PTR_ERR(core->dev);
		goto fail;
	}
	/* No cdev has been published yet: the node cannot dispatch an open. */
	vm_manager.vm_ops->vm_start(core);
	core->device = cvp_hfi_initialize(core->hfi_type, core->id,
				&core->resources, &cvp_handle_cmd_response);
	if (IS_ERR_OR_NULL(core->device)) {
		rc = PTR_ERR(core->device) ?: -EBADHANDLE;
		goto fail;
	}
	cvp_synx_ftbl_init(core);
	mutex_lock(&cvp_driver->lock);
	if (cvp_driver->num_cores >= MSM_CVP_CORES_MAX) {
		mutex_unlock(&cvp_driver->lock);
		rc = -EBUSY;
		goto fail;
	}
	list_add_tail(&core->list, &cvp_driver->cores);
	cvp_driver->num_cores++;
	core->listed = true;
	mutex_unlock(&cvp_driver->lock);
	cvp_driver->debugfs_root = msm_cvp_debugfs_init_drv();
	core->driver_debugfs_root = cvp_driver->debugfs_root;
	core->debugfs_root = msm_cvp_debugfs_init_core(core, cvp_driver->debugfs_root);
	cvp_driver->sku_version = core->resources.sku_version;
	rc = of_platform_populate(pdev->dev.of_node, msm_cvp_plat_match, NULL,
				  &pdev->dev);
	if (rc)
		goto fail;
	atomic64_set(&core->kernel_trans_id, ARRAY_SIZE(cvp_hfi_defs));
	if (core->resources.dsp_enabled) {
		rc = cvp_dsp_device_init();
		if (rc)
			goto fail;
	}
	rc = sysfs_create_group(&core->dev->kobj, &msm_cvp_core_attr_group);
	if (rc)
		goto fail;
	core->sysfs_registered = true;
	/* A standalone cdev remains allocated while a pending open holds its ref;
	 * open never derives or dereferences the enclosing hardware core.
	 */
	core->public_cdev = cdev_alloc();
	if (!core->public_cdev) {
		rc = -ENOMEM;
		goto fail;
	}
	core->public_cdev->owner = THIS_MODULE;
	core->public_cdev->ops = &cvp_fops;
	cvp_lifetime_publish(core);
	rc = cdev_add(core->public_cdev, core->dev_num, 1);
	if (rc) {
		kobject_put(&core->public_cdev->kobj);
		core->public_cdev = NULL;
		goto fail;
	}
	return 0;
fail:
	cvp_stop_core(core);
	of_platform_depopulate(&pdev->dev);
	dev_set_drvdata(&pdev->dev, NULL);
	cvp_free_core(core);
	return rc;
}

static int msm_cvp_probe_mem_cdsp(struct platform_device *pdev)
{
	return cvp_read_mem_cdsp_resources_from_dt(pdev);
}

static int msm_cvp_probe_context_bank(struct platform_device *pdev)
{
	return cvp_read_context_bank_resources_from_dt(pdev);
}

static int msm_cvp_probe_bus(struct platform_device *pdev)
{
	return cvp_read_bus_resources_from_dt(pdev);
}

static int msm_cvp_probe(struct platform_device *pdev)
{
	/*
	 * Sub devices probe will be triggered by of_platform_populate() towards
	 * the end of the probe function after msm-cvp device probe is
	 * completed. Return immediately after completing sub-device probe.
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,msm-cvp")) {
		return msm_probe_cvp_device(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-cvp,bus")) {
		return msm_cvp_probe_bus(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-cvp,context-bank")) {
		return msm_cvp_probe_context_bank(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-cvp,mem-cdsp")) {
		return msm_cvp_probe_mem_cdsp(pdev);
	}

	/* How did we end up here? */
	MSM_CVP_ERROR(1);
	return -EINVAL;
}

static int msm_cvp_remove(struct platform_device *pdev)
{
	bool main = of_device_is_compatible(pdev->dev.of_node, "qcom,msm-cvp");
	struct msm_cvp_core *core = main ? dev_get_drvdata(&pdev->dev) :
		dev_get_drvdata(pdev->dev.parent);

	if (!core)
		return 0;
	/* Removing a context-bank/bus first must stop its parent before devm
	 * destroys mappings or interconnects still used by hardware sessions.
	 */
	cvp_stop_core(core);
	if (!main)
		return 0;
	of_platform_depopulate(&pdev->dev);
	dev_set_drvdata(&pdev->dev, NULL);
	cvp_free_core(core);
	return 0;
}

static int msm_cvp_pm_suspend(struct device *dev)
{
	int rc = 0;
	struct msm_cvp_core *core;

	/*
	 * Bail out if
	 * - driver possibly not probed yet
	 * - not the main device. We don't support power management on
	 *   subdevices (e.g. context banks)
	 */
	if (!dev || !dev->driver ||
		!of_device_is_compatible(dev->of_node, "qcom,msm-cvp"))
		return 0;

	core = dev_get_drvdata(dev);
	if (!core) {
		dprintk(CVP_ERR, "%s invalid core\n", __func__);
		return -EINVAL;
	}

	rc = msm_cvp_suspend(core->id);
	if (rc == -ENOTSUPP)
		rc = 0;
	else if (rc)
		dprintk(CVP_WARN, "Failed to suspend: %d\n", rc);


	return rc;
}

static int msm_cvp_pm_resume(struct device *dev)
{
	dprintk(CVP_INFO, "%s\n", __func__);
	return 0;
}

static const struct dev_pm_ops msm_cvp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_cvp_pm_suspend, msm_cvp_pm_resume)
};

MODULE_DEVICE_TABLE(of, msm_cvp_plat_match);

static struct platform_driver msm_cvp_driver = {
	.probe = msm_cvp_probe,
	.remove = msm_cvp_remove,
	.driver = {
		.name = "msm_cvp",
		.of_match_table = msm_cvp_plat_match,
		.pm = &msm_cvp_pm_ops,
	},
};

static void cvp_destroy_caches(void)
{
	if (cvp_driver->smem_cache.cache)
		kmem_cache_destroy(cvp_driver->smem_cache.cache);
	if (cvp_driver->buf_cache.cache)
		kmem_cache_destroy(cvp_driver->buf_cache.cache);
	if (cvp_driver->frame_cache.cache)
		kmem_cache_destroy(cvp_driver->frame_cache.cache);
	if (cvp_driver->msg_cache.cache)
		kmem_cache_destroy(cvp_driver->msg_cache.cache);
}

static int __init msm_cvp_init(void)
{
	int rc = -ENOMEM;

	cvp_driver = kzalloc(sizeof(*cvp_driver), GFP_KERNEL);
	if (!cvp_driver)
		return -ENOMEM;
	INIT_LIST_HEAD(&cvp_driver->cores);
	mutex_init(&cvp_driver->lock);
	/* Driver registration can synchronously probe and publish a device. */
	cvp_driver->msg_cache.cache = KMEM_CACHE(cvp_session_msg, 0);
	cvp_driver->frame_cache.cache = KMEM_CACHE(msm_cvp_frame, 0);
	cvp_driver->buf_cache.cache = KMEM_CACHE(cvp_internal_buf, 0);
	cvp_driver->smem_cache.cache = KMEM_CACHE(msm_cvp_smem, 0);
	if (!cvp_driver->msg_cache.cache || !cvp_driver->frame_cache.cache ||
	    !cvp_driver->buf_cache.cache || !cvp_driver->smem_cache.cache)
		goto fail;
	rc = platform_driver_register(&msm_cvp_driver);
	if (!rc)
		return 0;
fail:
	cvp_destroy_caches();
	mutex_destroy(&cvp_driver->lock);
	kfree(cvp_driver);
	cvp_driver = NULL;
	return rc;
}

static void __exit msm_cvp_exit(void)
{
	/* Probe/remove, pending work and session cleanup still use these caches. */
	platform_driver_unregister(&msm_cvp_driver);
	cvp_dsp_device_exit();
	debugfs_remove_recursive(cvp_driver->debugfs_root);
	cvp_destroy_caches();
	mutex_destroy(&cvp_driver->lock);
	kfree(cvp_driver);
	cvp_driver = NULL;
}

module_init(msm_cvp_init);
module_exit(msm_cvp_exit);

MODULE_SOFTDEP("pre: msm-mmrm");
MODULE_LICENSE("GPL v2");
