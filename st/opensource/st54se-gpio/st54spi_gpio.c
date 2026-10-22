// SPDX-License-Identifier: GPL-2.0
/*
 * ST54SPI GPIO driver
 * Copyright (C) 2021 ST Microelectronics S.A.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *  * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **********************************************************************************/

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/mutex.h>

/* Flag ESE_CONF_GPIO_OPEN_RELEASE is to configure nRESET GPIO in
   open function and freeing GPIO in release function */
#define ESE_CONF_GPIO_OPEN_RELEASE

/* Flag ESE_CONF_GPIO_PROBE_REMOVE is to configure
   nRESET GPIO in probe function */
//#define ESE_CONF_GPIO_PROBE_REMOVE

#define ST54SPI_GPIO__MAGIC  0xEB
#define ST54SPI_GET_GPIO	_IOW(ST54SPI_GPIO__MAGIC, 0x01, unsigned int)
#define ST54SPI_SET_GPIO	_IOW(ST54SPI_GPIO__MAGIC, 0x02, unsigned int)

/* cdev_device_add pins this embedded device until all cdev users leave,
 * including opens already dispatched while a platform unbind is in progress.
 */
struct st54spi_gpio_device {
	dev_t st54spi_gpio_dev_t;
	struct cdev c_dev;
	struct class *class;
	struct device device;
	struct mutex lock;
	int gpiod_reset;
	bool removing;
	bool gpio_owned;
};

/** @brief   IOCTL function  to be used to set or get data from upper layer.
 *
 *  @param   pfile  fil node for opened device.
 *  @cmd     IOCTL type from upper layer.
 *  @arg     IOCTL arg from upper layer.
 *
 *  @return 0 on success, error code for failures.
 */
long st54spi_gpio_dev_ioctl(struct file *pfile, unsigned int cmd,
		unsigned long arg)
{
	struct st54spi_gpio_device *chip = pfile->private_data;
	int ret;

	if (!chip)
		return -ENODEV;
	mutex_lock(&chip->lock);
	if (chip->removing) {
		ret = -ENODEV;
		goto out;
	}
	switch (cmd) {
	case ST54SPI_GET_GPIO:
		ret = gpio_get_value(chip->gpiod_reset);
		break;
	case ST54SPI_SET_GPIO:
		if (arg > 1) {
			ret = -ENOIOCTLCMD;
			break;
		}
		gpio_set_value(chip->gpiod_reset, arg);
		ret = 0;
		break;
	default:
		ret = -ENOIOCTLCMD;
	}
out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int st54spi_gpio_dev_open(struct inode *inode, struct file *pfile)
{
	struct st54spi_gpio_device *chip = container_of(inode->i_cdev,
		struct st54spi_gpio_device, c_dev);
	int rc;

	mutex_lock(&chip->lock);
	if (chip->removing) {
		rc = -ENODEV;
		goto out;
	}
	/* Stock requests the reset GPIO for each open, excluding a second fd. */
	rc = gpio_request(chip->gpiod_reset, "gpio-power_nreset");
	if (rc < 0) {
		rc = -EFAULT;
		goto out;
	}
	rc = gpio_direction_output(chip->gpiod_reset, 0);
	if (rc < 0) {
		gpio_free(chip->gpiod_reset);
		rc = -EFAULT;
		goto out;
	}
	chip->gpio_owned = true;
	get_device(&chip->device);
	pfile->private_data = chip;
out:
	mutex_unlock(&chip->lock);
	return rc;
}

static int st54spi_gpio_dev_release(struct inode *inode, struct file *pfile)
{
	struct st54spi_gpio_device *chip = pfile->private_data;

	mutex_lock(&chip->lock);
	if (chip->gpio_owned) {
		gpio_free(chip->gpiod_reset);
		chip->gpio_owned = false;
	}
	mutex_unlock(&chip->lock);
	pfile->private_data = NULL;
	put_device(&chip->device);
	return 0;
}

static const struct file_operations st54spi_gpio_dev_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = st54spi_gpio_dev_open,
	.release = st54spi_gpio_dev_release,
	.unlocked_ioctl = st54spi_gpio_dev_ioctl,
};

static void st54spi_gpio_device_release(struct device *dev)
{
	struct st54spi_gpio_device *chip = container_of(dev,
		struct st54spi_gpio_device, device);

	kfree(chip);
}

static void st54spi_gpio_stop(struct st54spi_gpio_device *chip)
{
	mutex_lock(&chip->lock);
	chip->removing = true;
	if (chip->gpio_owned) {
		gpio_free(chip->gpiod_reset);
		chip->gpio_owned = false;
	}
	mutex_unlock(&chip->lock);
}

static int st54spi_gpio_probe(struct platform_device *pdev)
{
	struct st54spi_gpio_device *chip;
	int rc;

	if (!pdev->dev.of_node)
		return -ENOMEM;
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	mutex_init(&chip->lock);
	chip->gpiod_reset = of_get_named_gpio(pdev->dev.of_node,
		"gpio-power_nreset", 0);
	if (chip->gpiod_reset < 0) {
		rc = chip->gpiod_reset;
		goto free_chip;
	}
	if (!gpio_is_valid(chip->gpiod_reset)) {
		rc = -EFAULT;
		goto free_chip;
	}
	rc = alloc_chrdev_region(&chip->st54spi_gpio_dev_t, 0, 1,
		"st54spi_gpio");
	if (rc)
		goto free_chip;
	chip->class = class_create(THIS_MODULE, "st54spi_gpio");
	if (IS_ERR(chip->class)) {
		rc = PTR_ERR(chip->class);
		goto unregister_region;
	}
	device_initialize(&chip->device);
	chip->device.devt = chip->st54spi_gpio_dev_t;
	chip->device.class = chip->class;
	chip->device.release = st54spi_gpio_device_release;
	rc = dev_set_name(&chip->device, "st54spi_gpio");
	if (rc)
		goto put_device;
	dev_set_drvdata(&chip->device, chip);
	cdev_init(&chip->c_dev, &st54spi_gpio_dev_fops);
	chip->c_dev.owner = THIS_MODULE;
	rc = cdev_device_add(&chip->c_dev, &chip->device);
	if (rc) {
		/* cdev_device_add can dispatch open even when device_add fails. */
		st54spi_gpio_stop(chip);
		goto put_device;
	}
	platform_set_drvdata(pdev, chip);
	return 0;

put_device:
	class_destroy(chip->class);
	unregister_chrdev_region(chip->st54spi_gpio_dev_t, 1);
	put_device(&chip->device);
	return rc;
unregister_region:
	unregister_chrdev_region(chip->st54spi_gpio_dev_t, 1);
free_chip:
	kfree(chip);
	return rc;
}

static int st54spi_gpio_remove(struct platform_device *pdev)
{
	struct st54spi_gpio_device *chip = platform_get_drvdata(pdev);

	st54spi_gpio_stop(chip);
	cdev_device_del(&chip->c_dev, &chip->device);
	unregister_chrdev_region(chip->st54spi_gpio_dev_t, 1);
	class_destroy(chip->class);
	platform_set_drvdata(pdev, NULL);
	put_device(&chip->device);
	return 0;
}

static const struct of_device_id st54spi_gpio_of_match[] = {
	{ .compatible = "st,st54spi_gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, st54spi_gpio_of_match);

static struct platform_driver st54spi_gpio_driver = {
	.driver = {
		   .name = "st54spi_gpio",
		   .owner = THIS_MODULE,
		   .of_match_table = st54spi_gpio_of_match,
		   },
	.probe = st54spi_gpio_probe,
	.remove = st54spi_gpio_remove,
};

/* module load/unload record keeping */
static int __init st54spi_gpio_dev_init(void)
{
	pr_info("%s : Loading st54spi gpio_driver 1.0\n", __func__);
	return platform_driver_register(&st54spi_gpio_driver);
}

module_init(st54spi_gpio_dev_init);

static void __exit st54spi_gpio_dev_exit(void)
{
	pr_info("%s : Unloading st54spi gpio_driver 1.0\n", __func__);
	platform_driver_unregister(&st54spi_gpio_driver);
}

module_exit(st54spi_gpio_dev_exit);

MODULE_AUTHOR("STMicroelectronics");
MODULE_DESCRIPTION("ST54SPI GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("st54spi_gpio");

