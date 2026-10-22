// SPDX-License-Identifier: GPL-2.0
/*
 * Behavioral reconstruction from M2391's GPL-tagged mz_gesture_ts.ko.
 * .text: report 0x260, control store 0x75c, data show 0x88c;
 * .rodata+0x33c contains the key-to-mask-position table.
 * Primitive export CRCs match stock: FOD 1b75e0a2, Gesture 2505b27e,
 * setter 1ccef4cb, getter 3bec0fd6, registration de3a82ff. Report still requires
 * comparison with the stock input_dev definition and prototype.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <asm/unaligned.h>

#include "mz_gesture_ts.h"
#include "../../../../meizu/system/meizu_sys.h"

int FOD_switch_enable;
EXPORT_SYMBOL(FOD_switch_enable);
int Gesture_switch_enable;
EXPORT_SYMBOL(Gesture_switch_enable);

static DEFINE_MUTEX(gesture_lock);
static unsigned int gesture_data;
static u32 gesture_mask;
static unsigned char gesture_state;
static void (*gesture_handle)(uint8_t state);

static const struct {
	u16 code;
	u8 position;
} gesture_map[] = {
	{ 0x280, 1 }, { 0x281, 2 }, { 0x282, 3 }, { 0x283, 4 },
	{ 0x270, 5 }, { 0x271, 6 }, { 0x272, 25 }, { 0x290, 8 },
	{ 0x291, 9 }, { 0x292, 10 }, { 0x293, 11 }, { 0x294, 12 },
	{ 0x295, 13 }, { 0x296, 14 }, { 0x29a, 15 },
};

int mz_gesture_report(struct input_dev *input, unsigned int code, unsigned int value)
{
	unsigned int i;
	int ret = 0;

	/* Stock drops reports rather than waiting while controls are changing. */
	if (!mutex_trylock(&gesture_lock))
		return 0;
	if (!gesture_state)
		goto out;
	for (i = 0; i < ARRAY_SIZE(gesture_map); i++)
		if (gesture_map[i].code == code)
			break;
	if (i == ARRAY_SIZE(gesture_map))
		goto out;
	if (!(gesture_mask & BIT(gesture_map[i].position - 1))) {
		gesture_data = 0;
		goto out;
	}
	if (value != 0 && value != 1 && value != 2) {
		gesture_data = 0;
		goto out;
	}
	gesture_data = code;
	input_event(input, EV_KEY, code, value == 2 ? 1 : value);
	input_sync(input);
	if (value == 2) {
		input_event(input, EV_KEY, code, 0);
		input_sync(input);
		ret = 1;
	}
out:
	mutex_unlock(&gesture_lock);
	return ret;
}
EXPORT_SYMBOL(mz_gesture_report);

void mz_gesture_set_gesture_statu(bool state)
{
	mutex_lock(&gesture_lock);
	gesture_state = state;
	mutex_unlock(&gesture_lock);
}
EXPORT_SYMBOL(mz_gesture_set_gesture_statu);

bool mz_gesture_get_gesture_statu(void)
{
	return READ_ONCE(gesture_state) != 0;
}
EXPORT_SYMBOL(mz_gesture_get_gesture_statu);

void mz_gesture_handle_register(void (*handle)(uint8_t state))
{
	mutex_lock(&gesture_lock);
	gesture_handle = handle;
	mutex_unlock(&gesture_lock);
}
EXPORT_SYMBOL(mz_gesture_handle_register);

static ssize_t gesture_control_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&gesture_lock);
	ret = sysfs_emit(buf, "%x\n", gesture_mask);
	mutex_unlock(&gesture_lock);
	return ret;
}

static ssize_t gesture_control_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u32 control;

	/* This ABI is binary, not a textual number. Reject stock's short-read bug. */
	if (count < sizeof(control))
		return -EINVAL;
	control = get_unaligned_le32(buf);
	mutex_lock(&gesture_lock);
	FOD_switch_enable = control & BIT(24);
	Gesture_switch_enable = control & 0x11f;
	gesture_state = !!(control & BIT(31));
	gesture_mask = gesture_state ? control : 0;
	/* Stock invokes the callback under this mutex; it must not re-enter us. */
	if (gesture_handle)
		gesture_handle(gesture_state);
	mutex_unlock(&gesture_lock);
	return count;
}
static DEVICE_ATTR_RW(gesture_control);

static ssize_t gesture_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&gesture_lock);
	ret = sysfs_emit(buf, "%u\n", gesture_data);
	gesture_data = 0;
	mutex_unlock(&gesture_lock);
	return ret;
}

static ssize_t gesture_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return -EPERM;
}
static DEVICE_ATTR_RW(gesture_data);

static int mz_gesture_probe(struct platform_device *pdev)
{
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_gesture_control);
	if (ret)
		return ret;
	ret = device_create_file(&pdev->dev, &dev_attr_gesture_data);
	if (ret)
		goto remove_control;
	ret = meizu_sysfslink_register_name(&pdev->dev, "tp");
	if (!ret)
		return 0;
	device_remove_file(&pdev->dev, &dev_attr_gesture_data);
remove_control:
	device_remove_file(&pdev->dev, &dev_attr_gesture_control);
	return ret;
}

static int mz_gesture_remove(struct platform_device *pdev)
{
	meizu_sysfslink_remove_name("tp");
	device_remove_file(&pdev->dev, &dev_attr_gesture_data);
	device_remove_file(&pdev->dev, &dev_attr_gesture_control);
	return 0;
}

static struct platform_driver mz_gesture_driver = {
	.probe = mz_gesture_probe,
	.remove = mz_gesture_remove,
	.driver.name = "mx-gs",
};
static struct platform_device *mz_gesture_device;

static int __init mz_gesture_init(void)
{
	int ret;

	ret = platform_driver_register(&mz_gesture_driver);
	if (ret)
		return ret;
	mz_gesture_device = platform_device_register_simple("mx-gs", -1, NULL, 0);
	if (IS_ERR(mz_gesture_device)) {
		ret = PTR_ERR(mz_gesture_device);
		platform_driver_unregister(&mz_gesture_driver);
		return ret;
	}
	return 0;
}

static void __exit mz_gesture_exit(void)
{
	platform_device_unregister(mz_gesture_device);
	platform_driver_unregister(&mz_gesture_driver);
}
module_init(mz_gesture_init);
module_exit(mz_gesture_exit);

MODULE_DESCRIPTION("Meizu M2391 gesture control and event reporting");
MODULE_LICENSE("GPL");
