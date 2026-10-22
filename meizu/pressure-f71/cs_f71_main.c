// SPDX-License-Identifier: GPL-2.0-only
/* Meizu M2391 EVB/B-series F71 pressure controller, independently recovered. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "../boot-mode/boot_mode.h"
#include "cs_f71.h"

static DEFINE_MUTEX(cs_f71_bind_lock);
static bool cs_f71_present;
static bool cs_f71_driver_registered;

static void cs_f71_stop(struct cs_f71 *cs)
{
	if (cs->stopped)
		return;
	mutex_lock(&cs->command_lock);
	WRITE_ONCE(cs->removed, true);
	mutex_unlock(&cs->command_lock);
	/* Drain all external producers before destroying their work/IRQs. */
	cs_f71_proc_exit(cs);
	if (cs->misc_registered) {
		cs_f71_misc_exit(cs);
		cs->misc_registered = false;
	}
	cs_f71_runtime_exit(cs);
	cs_f71_firmware_work_exit(cs);
	cs_f71_irq_exit(cs);
	cs_f71_input_exit(cs);
	cs_f71_power_exit(cs);
	cs->stopped = true;
}

static int cs_f71_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct cs_f71 *cs;
	int ret;

	mutex_lock(&cs_f71_bind_lock);
	if (cs_f71_present) {
		ret = -EBUSY;
		goto unlock;
	}
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		ret = -EOPNOTSUPP;
		goto unlock;
	}
	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs) {
		ret = -ENOMEM;
		goto unlock;
	}
	cs->client = client;
	kref_init(&cs->ref);
	mutex_init(&cs->io_lock);
	mutex_init(&cs->command_lock);
	mutex_init(&cs->operation_lock);
	cs_f71_firmware_work_init(cs);
	ret = cs_f71_power_init(cs);
	if (ret)
		goto power;
	ret = cs_f71_input_init(cs);
	if (ret)
		goto power;
	ret = cs_f71_irq_init(cs);
	if (ret)
		goto input;
	ret = cs_f71_runtime_init(cs);
	if (ret)
		goto irq;
	i2c_set_clientdata(client, cs);
	/* All callback-visible state exists before proc/misc publication. */
	ret = cs_f71_proc_init(cs);
	if (ret)
		goto stop;
	ret = cs_f71_misc_init(cs);
	if (ret)
		goto stop;
	cs->misc_registered = true;
	cs_f71_present = true;
	cs_f71_runtime_start(cs);
	mutex_unlock(&cs_f71_bind_lock);
	return 0;
stop:
	cs_f71_stop(cs);
	i2c_set_clientdata(client, NULL);
	goto put;
irq:
	cs_f71_irq_exit(cs);
input:
	cs_f71_input_exit(cs);
power:
	cs_f71_power_exit(cs);
put:
	cs_f71_put(cs);
unlock:
	mutex_unlock(&cs_f71_bind_lock);
	return ret;
}

static int cs_f71_remove(struct i2c_client *client)
{
	struct cs_f71 *cs;

	mutex_lock(&cs_f71_bind_lock);
	cs = i2c_get_clientdata(client);
	if (cs) {
		cs_f71_stop(cs);
		i2c_set_clientdata(client, NULL);
		cs_f71_present = false;
		cs_f71_put(cs);
	}
	mutex_unlock(&cs_f71_bind_lock);
	return 0;
}

static void cs_f71_shutdown(struct i2c_client *client)
{
	struct cs_f71 *cs;

	mutex_lock(&cs_f71_bind_lock);
	cs = i2c_get_clientdata(client);
	if (cs)
		cs_f71_stop(cs);
	mutex_unlock(&cs_f71_bind_lock);
}

static const struct of_device_id cs_f71_of_match[] = {
	{ .compatible = "meizu,cs_press_f71" },
	{ },
};
MODULE_DEVICE_TABLE(of, cs_f71_of_match);

static const struct i2c_device_id cs_f71_ids[] = {
	{ "cs_press", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, cs_f71_ids);

static struct i2c_driver cs_f71_driver = {
	.driver = {
		.name = "cs_press",
		.of_match_table = cs_f71_of_match,
	},
	.probe = cs_f71_probe,
	.remove = cs_f71_remove,
	.shutdown = cs_f71_shutdown,
	.id_table = cs_f71_ids,
};

static int __init cs_f71_init(void)
{
	int mode = get_boot_mode();
	int ret;

	/* Original init_module explicitly skips modes2 and3 (unlike M68). */
	if (mode == 2 || mode == 3)
		return 0;
	ret = i2c_add_driver(&cs_f71_driver);
	if (!ret)
		cs_f71_driver_registered = true;
	return ret;
}

static void __exit cs_f71_exit(void)
{
	if (cs_f71_driver_registered)
		i2c_del_driver(&cs_f71_driver);
}

module_init(cs_f71_init);
module_exit(cs_f71_exit);
MODULE_DESCRIPTION("Meizu F71 pressure controller");
MODULE_LICENSE("GPL");
