// SPDX-License-Identifier: GPL-2.0-only
/* Meizu M2391 M68 pressure controller. Recovered from its original module. */
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "../system/meizu_sys.h"
#include "../boot-mode/boot_mode.h"
#include "cs_m68.h"

static DEFINE_MUTEX(cs_m68_device_lock);
static bool cs_m68_present;

static const struct attribute_group *cs_m68_groups[] = {
	&cs_m68_control_group,
	&cs_m68_pressure_group,
	&cs_m68_calibration_group,
	&cs_m68_diagnostic_group,
	NULL,
};

static void cs_m68_unpublish(struct cs_m68 *cs)
{
	if (cs->sysfs_created) {
		sysfs_remove_groups(&cs->client->dev.kobj, cs_m68_groups);
		cs->sysfs_created = false;
	}
	if (cs->misc_registered) {
		misc_deregister(&cs->misc);
		cs->misc_registered = false;
	}
	if (cs->root_link_created) {
		sysfs_remove_link(cs->root_kobject, "cs_press");
		cs->root_link_created = false;
	}
	if (cs->meizu_link_created) {
		meizu_sysfslink_remove_name("haptic");
		cs->meizu_link_created = false;
	}
	kobject_put(cs->root_kobject);
	cs->root_kobject = NULL;
}

static void cs_m68_shutdown(struct cs_m68 *cs)
{
	mutex_lock(&cs->access_lock);
	WRITE_ONCE(cs->removed, true);
	mutex_unlock(&cs->access_lock);
	cs_m68_unpublish(cs);
	cs_m68_stop_runtime(cs);
	if (cs->irq_live) {
		free_irq(cs->irq, cs);
		cs->irq_live = false;
	}
	cs_m68_stop_events(cs);
	if (cs->input) {
		input_unregister_device(cs->input);
		cs->input = NULL;
	}
	cs_m68_release_power(cs);
}

static int cs_m68_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct cs_m68 *cs;
	struct input_dev *input;
	int ret;

	mutex_lock(&cs_m68_device_lock);
	if (cs_m68_present) {
		ret = -EBUSY;
		goto unlock;
	}
	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs) {
		ret = -ENOMEM;
		goto unlock;
	}
	cs->client = client;
	cs->irq_gpio = -EINVAL;
	cs->enable_gpio = -EINVAL;
	cs->calibration_value = 100;
	cs->threshold_down = 60;
	cs->threshold_up = 50;
	cs->vibrate_down = 50;
	cs->vibrate_up = 50;
	kref_init(&cs->refcount);
	mutex_init(&cs->access_lock);
	mutex_init(&cs->io_lock);
	mutex_init(&cs->command_lock);
	mutex_init(&cs->slot_lock);
	mutex_init(&cs->irq_lock);
	i2c_set_clientdata(client, cs);
	/* Logging has a separate legacy binary representation in slot73. */
	cs_m68_load_log_mask(cs);
	ret = cs_m68_prepare_power(cs);
	if (ret)
		goto fail;
	input = input_allocate_device();
	if (!input) {
		ret = -ENOMEM;
		goto fail;
	}
	input->name = "ndt";
	input->dev.parent = &client->dev;
	input_set_capability(input, EV_KEY, 701);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 65535, 0, 0);
	ret = input_register_device(input);
	if (ret) {
		input_free_device(input);
		goto fail;
	}
	cs->input = input;
	ret = cs_m68_start_events(cs);
	if (ret)
		goto fail;
	cs->irq = gpio_to_irq(cs->irq_gpio);
	if (cs->irq < 0) {
		ret = cs->irq;
		goto fail;
	}
	ret = request_threaded_irq(cs->irq, NULL, cs_m68_interrupt,
		IRQF_ONESHOT | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		dev_name(&client->dev), cs);
	if (ret)
		goto fail;
	cs->irq_live = true;
	cs->irq_enabled = true;
	cs_m68_set_irq(cs, false);
	ret = cs_m68_start_runtime(cs);
	if (ret)
		goto fail;
	cs->root_kobject = kobject_create_and_add("cs_press", NULL);
	if (!cs->root_kobject) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = meizu_sysfslink_register_name(&client->dev, "haptic");
	if (ret)
		goto fail;
	cs->meizu_link_created = true;
	ret = sysfs_create_link(cs->root_kobject, &client->dev.kobj, "cs_press");
	if (ret)
		goto fail;
	cs->root_link_created = true;
	cs->misc.minor = MISC_DYNAMIC_MINOR;
	cs->misc.name = "ndt";
	cs->misc.fops = &cs_m68_file_operations;
	cs->misc.parent = &client->dev;
	ret = misc_register(&cs->misc);
	if (ret)
		goto fail;
	cs->misc_registered = true;
	ret = sysfs_create_groups(&client->dev.kobj, cs_m68_groups);
	if (ret)
		goto fail;
	cs->sysfs_created = true;
	cs_m68_present = true;
	mutex_unlock(&cs_m68_device_lock);
	return 0;
fail:
	cs_m68_shutdown(cs);
	i2c_set_clientdata(client, NULL);
	cs_m68_put(cs);
unlock:
	mutex_unlock(&cs_m68_device_lock);
	return ret;
}

static int cs_m68_remove(struct i2c_client *client)
{
	struct cs_m68 *cs = i2c_get_clientdata(client);

	mutex_lock(&cs_m68_device_lock);
	cs_m68_shutdown(cs);
	i2c_set_clientdata(client, NULL);
	cs_m68_present = false;
	cs_m68_put(cs);
	mutex_unlock(&cs_m68_device_lock);
	return 0;
}

static int cs_m68_suspend(struct device *dev)
{
	struct cs_m68 *cs = dev_get_drvdata(dev);

	/* Original PM callbacks only prevent suspend until update_work ends. */
	return READ_ONCE(cs->initialized) ? 0 : -EBUSY;
}

static int cs_m68_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops cs_m68_pm_ops = {
	.suspend = cs_m68_suspend,
	.resume = cs_m68_resume,
	.runtime_suspend = cs_m68_suspend,
	.runtime_resume = cs_m68_resume,
};

static const struct of_device_id cs_m68_of_match[] = {
	{ .compatible = "meizu,cs_press_m68" },
	{ },
};
MODULE_DEVICE_TABLE(of, cs_m68_of_match);

static const struct i2c_device_id cs_m68_id[] = {
	{ "ndt", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cs_m68_id);

static struct i2c_driver cs_m68_driver = {
	.driver = {
		.name = "ndt",
		.of_match_table = cs_m68_of_match,
		.pm = &cs_m68_pm_ops,
	},
	.probe = cs_m68_probe,
	.remove = cs_m68_remove,
	.id_table = cs_m68_id,
};
static bool cs_m68_driver_registered;

static int __init cs_m68_init(void)
{
	int mode = get_boot_mode();
	int ret;

	/* Stock init's mode==1 branch skips its diagnostic print only: both
	 * paths join at i2c_register_driver. Unlike haptic, no mode is filtered.
	 */
	if (mode != 1)
		pr_info("cs_press_m68: boot mode %d\n", mode);
	ret = i2c_add_driver(&cs_m68_driver);
	if (!ret)
		cs_m68_driver_registered = true;
	return ret;
}

static void __exit cs_m68_exit(void)
{
	if (cs_m68_driver_registered)
		i2c_del_driver(&cs_m68_driver);
}
module_init(cs_m68_init);
module_exit(cs_m68_exit);

MODULE_DESCRIPTION("Meizu M2391 M68 pressure controller");
MODULE_LICENSE("GPL");
