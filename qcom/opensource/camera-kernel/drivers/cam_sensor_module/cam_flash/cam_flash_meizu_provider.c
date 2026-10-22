// SPDX-License-Identifier: GPL-2.0-only
/* Original M2391 SI SY7808 provider registration, with source-owned objects
 * and a registry lock which drains active operations before device removal.
 * The original seven-slot callback registry is shared by HAL and factories.
 */
#include <linux/i2c.h>
#include <linux/of.h>
#include "cam_flash_meizu_sy7808.h"
#include "cam_flash_meizu_provider.h"

struct meizu_flash_provider {
	struct meizu_sy7808 chip;
	struct camera_io_master io;
	u32 index;
};
static DEFINE_MUTEX(meizu_flash_registry_lock);
static struct meizu_flash_provider *meizu_flash_providers[3];
static struct meizu_flashlight_operations *meizu_flash_ops[3];
static struct meizu_flashlight_operations meizu_sy7808_ops;
static_assert(sizeof(struct meizu_flashlight_operations) == 56);
static_assert(offsetof(struct meizu_flashlight_operations, init) == 0);
static_assert(offsetof(struct meizu_flashlight_operations, enable) == 16);
static_assert(offsetof(struct meizu_flashlight_operations, disable) == 24);
static_assert(offsetof(struct meizu_flashlight_operations, duty) == 32);
static_assert(offsetof(struct meizu_flashlight_operations, release) == 40);
static_assert(offsetof(struct meizu_flashlight_operations, shutdown) == 48);

/* Original registry permits replacement, including clearing with NULL. Invalid
 * IDs trap in stock; source rejects them without touching another slot.
 * Unregister holds the same lock as invocation, draining active callbacks.
 */
int flashlight_dev_register(uint32_t index, struct meizu_flashlight_operations *ops)
{
	if (index >= ARRAY_SIZE(meizu_flash_ops)) return -EINVAL;
	mutex_lock(&meizu_flash_registry_lock);
	meizu_flash_ops[index] = ops;
	mutex_unlock(&meizu_flash_registry_lock);
	return 0;
}
EXPORT_SYMBOL(flashlight_dev_register);

int flashlight_dev_unregister(uint32_t index)
{
	return flashlight_dev_register(index, NULL);
}
EXPORT_SYMBOL(flashlight_dev_unregister);

static int meizu_flash_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct meizu_flash_provider *provider;
	u32 index, address;
	int rc;

	rc = of_property_read_u32(client->dev.of_node, "cell-index", &index);
	if (rc)
		return rc;
	rc = of_property_read_u32(client->dev.of_node, "slave-addr", &address);
	if (rc)
		return rc;
	if (index >= ARRAY_SIZE(meizu_flash_providers) || address > 0xff)
		return -EINVAL;
	provider = devm_kzalloc(&client->dev, sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return -ENOMEM;
	provider->index = index;
	provider->io.master_type = I2C_MASTER;
	provider->io.client = client;
	provider->chip.io = &provider->io;
	mutex_init(&provider->chip.lock);
	/* Qualcomm QUP helper expects the original 8-bit slave address and
	 * shifts it right once for i2c_transfer; DT M2391 supplies 0xc6.
	 */
	client->addr = address;
	mutex_lock(&meizu_flash_registry_lock);
	if (meizu_flash_providers[index]) {
		mutex_unlock(&meizu_flash_registry_lock);
		return -EBUSY;
	}
	i2c_set_clientdata(client, provider);
	meizu_flash_providers[index] = provider;
	meizu_flash_ops[index] = &meizu_sy7808_ops;
	mutex_unlock(&meizu_flash_registry_lock);
	return 0;
}

static int meizu_flash_i2c_remove(struct i2c_client *client)
{
	struct meizu_flash_provider *provider = i2c_get_clientdata(client);

	mutex_lock(&meizu_flash_registry_lock);
	if (provider && meizu_flash_providers[provider->index] == provider) {
		meizu_flash_ops[provider->index] = NULL;
		meizu_flash_providers[provider->index] = NULL;
	}
	i2c_set_clientdata(client, NULL);
	mutex_unlock(&meizu_flash_registry_lock);
	return 0;
}

static const struct of_device_id meizu_flash_i2c_match[] = {
	{ .compatible = "si,sy7808" }, { },
};
static const struct i2c_device_id meizu_flash_i2c_ids[] = {
	{ "meizu-sy7808", 0 }, { },
};
static struct i2c_driver meizu_flash_i2c_driver = {
	.driver = {
		.name = "meizu-sy7808", .of_match_table = meizu_flash_i2c_match,
		.suppress_bind_attrs = true,
	},
	.probe = meizu_flash_i2c_probe, .remove = meizu_flash_i2c_remove,
	.id_table = meizu_flash_i2c_ids,
};

int cam_meizu_flash_register(void)
{
	return i2c_add_driver(&meizu_flash_i2c_driver);
}

void cam_meizu_flash_unregister(void)
{
	i2c_del_driver(&meizu_flash_i2c_driver);
}

/* All operations hold registry lifetime protection before taking the chip
 * lock. No chip operation or removal takes these locks in reverse order.
 */
enum meizu_flash_operation {
	MEIZU_FLASH_INIT, MEIZU_FLASH_RELEASE, MEIZU_FLASH_SHUTDOWN,
	MEIZU_FLASH_ENABLE, MEIZU_FLASH_DISABLE, MEIZU_FLASH_DUTY,
};
static int meizu_flash_invoke(unsigned int index,
		enum meizu_flash_operation operation, u32 first, u32 second)
{
	struct meizu_flashlight_operations *ops;
	int rc;

	if (index >= ARRAY_SIZE(meizu_flash_providers))
		return -EINVAL;
	mutex_lock(&meizu_flash_registry_lock);
	if (!meizu_flash_ops[index]) {
		rc = -ENODEV;
		goto out;
	}
	ops = meizu_flash_ops[index];
	switch (operation) {
	case MEIZU_FLASH_INIT: rc = ops->init ? ops->init(index) : -EOPNOTSUPP; break;
	case MEIZU_FLASH_RELEASE: rc = ops->release ? ops->release(index) : -EOPNOTSUPP; break;
	case MEIZU_FLASH_SHUTDOWN: rc = ops->shutdown ? ops->shutdown(index) : -EOPNOTSUPP; break;
	case MEIZU_FLASH_ENABLE: rc = ops->enable ? ops->enable(index, first, second) : -EOPNOTSUPP; break;
	case MEIZU_FLASH_DISABLE: rc = ops->disable ? ops->disable(index, first) : -EOPNOTSUPP; break;
	case MEIZU_FLASH_DUTY: rc = ops->duty ? ops->duty(index, first, second) : -EOPNOTSUPP; break;
	default: rc = -EINVAL; break;
	}
out:
	mutex_unlock(&meizu_flash_registry_lock);
	return rc;
}
int cam_meizu_flash_provider_init(unsigned int index)
{
	return meizu_flash_invoke(index, MEIZU_FLASH_INIT, 0, 0);
}
int cam_meizu_flash_provider_release(unsigned int index)
{
	return meizu_flash_invoke(index, MEIZU_FLASH_RELEASE, 0, 0);
}
int cam_meizu_flash_provider_shutdown(unsigned int index)
{
	return meizu_flash_invoke(index, MEIZU_FLASH_SHUTDOWN, 0, 0);
}
int cam_meizu_flash_provider_enable(unsigned int index, unsigned int mode,
		unsigned int channels)
{
	return meizu_flash_invoke(index, MEIZU_FLASH_ENABLE, mode, channels);
}
int cam_meizu_flash_provider_disable(unsigned int index, unsigned int channels)
{
	return meizu_flash_invoke(index, MEIZU_FLASH_DISABLE, channels, 0);
}
int cam_meizu_flash_provider_duty(unsigned int index, unsigned int mode,
		unsigned int duty)
{
	return meizu_flash_invoke(index, MEIZU_FLASH_DUTY, mode, duty);
}

int cam_meizu_flash_provider_factory(unsigned int index, unsigned int selector,
		unsigned int command, unsigned int value, int brightness, unsigned int extra)
{
	int rc;

	if (index >= ARRAY_SIZE(meizu_flash_providers))
		return -EINVAL;
	mutex_lock(&meizu_flash_registry_lock);
	if (!meizu_flash_providers[index])
		rc = -ENODEV;
	else
		rc = meizu_sy7808_factory(&meizu_flash_providers[index]->chip,
			selector, command, value, brightness, extra);
	mutex_unlock(&meizu_flash_registry_lock);
	return rc;
}

/* Builtin callbacks are invoked with registry lifetime held, then take only
 * the chip lock. They never recurse into the public registry operations.
 */
static int meizu_provider_init(uint32_t index)
{
	return meizu_sy7808_init(&meizu_flash_providers[index]->chip);
}
static int meizu_provider_release(uint32_t index)
{
	return meizu_sy7808_release(&meizu_flash_providers[index]->chip);
}
static int meizu_provider_shutdown(uint32_t index)
{
	return meizu_sy7808_shutdown(&meizu_flash_providers[index]->chip);
}
static int meizu_provider_enable(uint32_t index, uint32_t mode, uint32_t channels)
{
	return meizu_sy7808_enable(&meizu_flash_providers[index]->chip, mode, channels);
}
static int meizu_provider_disable(uint32_t index, uint32_t channels)
{
	return meizu_sy7808_disable(&meizu_flash_providers[index]->chip, channels);
}
static int meizu_provider_duty(uint32_t index, uint32_t mode, uint32_t duty)
{
	return meizu_sy7808_set_duty(&meizu_flash_providers[index]->chip, mode, duty);
}
static struct meizu_flashlight_operations meizu_sy7808_ops = {
	.init = meizu_provider_init, .enable = meizu_provider_enable,
	.disable = meizu_provider_disable, .duty = meizu_provider_duty,
	.release = meizu_provider_release, .shutdown = meizu_provider_shutdown,
};
