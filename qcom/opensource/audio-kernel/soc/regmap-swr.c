// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/init.h>
#include <soc/soundwire.h>
#include <linux/unaligned.h>

/* Qualcomm SoundWire codecs use 16-bit register offsets and 8-bit values. */
static int regmap_swr_gather_write(void *context, const void *reg,
				   size_t reg_size, const void *val, size_t len)
{
	struct swr_device *swr = context;
	const u8 *values = val;
	unsigned int addr;
	size_t i;
	int ret;

	if (reg_size != sizeof(u16))
		return -EINVAL;
	addr = get_unaligned((const u16 *)reg);
	if (len > U16_MAX - addr + 1)
		return -ERANGE;
	for (i = 0; i < len; i++) {
		u8 value = values[i];

		ret = swr_write(swr, swr->dev_num, addr + i, &value);
		if (ret)
			return ret;
	}
	return 0;
}

static int regmap_swr_write(void *context, const void *data, size_t count)
{
	struct swr_device *swr = context;
	const u8 *entry = data;
	size_t num = count / 3;
	u16 *regs;
	u8 *values;
	int ret;
	size_t i;

	if (!num || count % 3)
		return -EINVAL;
	if (num == 1)
		return regmap_swr_gather_write(swr, entry, sizeof(u16),
					       entry + sizeof(u16), 1);

	regs = kmalloc_array(num, sizeof(*regs), GFP_KERNEL);
	if (!regs)
		return -ENOMEM;
	values = kmalloc(num, GFP_KERNEL);
	if (!values) {
		kfree(regs);
		return -ENOMEM;
	}
	for (i = 0; i < num; i++, entry += 3) {
		regs[i] = get_unaligned((const u16 *)entry);
		values[i] = entry[2];
	}
	ret = swr_bulk_write(swr, swr->dev_num, regs, values, num);
	kfree(values);
	kfree(regs);
	return ret;
}

static int regmap_swr_read(void *context, const void *reg, size_t reg_size,
			   void *val, size_t len)
{
	struct swr_device *swr = context;
	unsigned int addr;

	if (reg_size != sizeof(u16))
		return -EINVAL;
	addr = get_unaligned((const u16 *)reg);
	if (len > U16_MAX - addr + 1)
		return -ERANGE;
	return swr_read(swr, swr->dev_num, addr, val, len);
}

static struct regmap_bus regmap_swr = {
	.write = regmap_swr_write,
	.gather_write = regmap_swr_gather_write,
	.read = regmap_swr_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

struct regmap *__regmap_init_swr(struct swr_device *swr,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	if (config->reg_bits != 16 || config->val_bits != 8 || config->pad_bits)
		return ERR_PTR(-EINVAL);
	return __regmap_init(&swr->dev, &regmap_swr, swr, config,
			   lock_key, lock_name);
}
EXPORT_SYMBOL(__regmap_init_swr);

struct regmap *__devm_regmap_init_swr(struct swr_device *swr,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	if (config->reg_bits != 16 || config->val_bits != 8 || config->pad_bits)
		return ERR_PTR(-EINVAL);
	return __devm_regmap_init(&swr->dev, &regmap_swr, swr, config,
				lock_key, lock_name);
}
EXPORT_SYMBOL(__devm_regmap_init_swr);

MODULE_LICENSE("GPL v2");
