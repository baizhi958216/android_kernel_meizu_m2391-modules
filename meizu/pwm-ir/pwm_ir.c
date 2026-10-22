// SPDX-License-Identifier: GPL-2.0-or-later
/* M2391 clock/pinctrl IR protocol recovered from original pwm_ir.ko
 * .text+0x1ac..0x780. The original clk_set_duty_cycle lookup is replaced
 * with its public GPL export. Validate all resources and drain sysfs before
 * devres tears down the RC device; serialize the entire clock transaction.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <media/rc-core.h>

struct pwm_ir {
 struct mutex lock;
 struct pinctrl *pins;
 struct pinctrl_state *active, *sleep;
 struct clk *clk;
 struct rc_dev *rc;
 unsigned int carrier, duty_cycle;
};

static int set_carrier_locked(struct pwm_ir *ir, unsigned int carrier)
{
 if (!carrier)
  return -EINVAL;
 if (carrier > 37000 && carrier < 39000)
  ir->carrier = 38000;
 else if (carrier == 36000)
  ir->carrier = 36000;
 else
  pr_err("pwm_ir not support carrier=%d\n", carrier);
 return 0; /* Original retains previous carrier for other nonzero values. */
}
static int pwm_ir_set_carrier(struct rc_dev *rc, u32 carrier)
{
 struct pwm_ir *ir = rc->priv;
 int ret;
 mutex_lock(&ir->lock);
 ret = set_carrier_locked(ir, carrier);
 mutex_unlock(&ir->lock);
 return ret;
}
static int pwm_ir_set_duty_cycle(struct rc_dev *rc, u32 duty)
{
 struct pwm_ir *ir = rc->priv;
 if (duty > 100)
  return -EINVAL;
 mutex_lock(&ir->lock);
 ir->duty_cycle = duty;
 mutex_unlock(&ir->lock);
 return 0;
}
static int pwm_ir_tx_locked(struct pwm_ir *ir, unsigned int *buffer,
       unsigned int count)
{
 unsigned int i, period, duty;
 int ret;
 ret = clk_set_rate(ir->clk, ir->carrier);
 if (ret)
  return ret;
 period = DIV_ROUND_CLOSEST(NSEC_PER_SEC, ir->carrier);
 duty = DIV_ROUND_CLOSEST(ir->duty_cycle * period, 100);
 ret = clk_set_duty_cycle(ir->clk, duty, period);
 if (ret)
  return ret;
 ret = clk_prepare_enable(ir->clk);
 if (ret)
  return ret;
 for (i = 0; i < count; i++) {
  ktime_t edge = ktime_get();
  s64 delta;
  ret = pinctrl_select_state(ir->pins, i & 1 ? ir->sleep : ir->active);
  if (ret)
   break;
  delta = ktime_us_delta(ktime_add_us(edge, buffer[i]), ktime_get());
  if (delta > 0)
   udelay(delta);
 }
 /* Always leave the output inactive, including an odd-length RC buffer. */
 pinctrl_select_state(ir->pins, ir->sleep);
 clk_disable_unprepare(ir->clk);
 return ret ? ret : count;
}
static int pwm_ir_tx(struct rc_dev *rc, unsigned int *buffer, unsigned int count)
{
 struct pwm_ir *ir = rc->priv;
 int ret;
 mutex_lock(&ir->lock);
 ret = pwm_ir_tx_locked(ir, buffer, count);
 mutex_unlock(&ir->lock);
 return ret;
}
static ssize_t ir_transmit_show(struct device *dev, struct device_attribute *attr,
          char *buffer)
{
 struct rc_dev *rc = to_rc_dev(dev);
 struct pwm_ir *ir = rc->priv;
 ssize_t ret;
 mutex_lock(&ir->lock);
 ret = scnprintf(buffer, PAGE_SIZE, "carrier=%d,  cycle=%d ,device_name=%s \n",
   ir->carrier, ir->duty_cycle, rc->device_name);
 mutex_unlock(&ir->lock);
 return ret;
}
static ssize_t ir_transmit_store(struct device *dev, struct device_attribute *attr,
           const char *buffer, size_t count)
{
 struct pwm_ir *ir = to_rc_dev(dev)->priv;
 unsigned int waveform[68], carrier, i;
 int ret;
 if (kstrtouint(buffer, 10, &carrier))
  return -EINVAL;
 waveform[0] = 9000;
 waveform[1] = 4500;
 for (i = 2; i < ARRAY_SIZE(waveform); i++)
  waveform[i] = (i & 1) && i >= 37 && i <= 51 ? 1680 : 560;
 mutex_lock(&ir->lock);
 ret = set_carrier_locked(ir, carrier);
 if (ret)
  goto out;
 for (i = 0; i < 3; i++) {
  ret = pwm_ir_tx_locked(ir, waveform, ARRAY_SIZE(waveform));
  if (ret < 0)
   goto out;
  msleep(600);
 }
 ret = count;
out:
 mutex_unlock(&ir->lock);
 return ret;
}
static struct device_attribute dev_attr_transmit =
 __ATTR(transmit, 0664, ir_transmit_show, ir_transmit_store);
static void remove_transmit(void *data)
{
 struct rc_dev *rc = data;
 device_remove_file(&rc->dev, &dev_attr_transmit);
}
static int pwm_ir_probe(struct platform_device *pdev)
{
 struct device *dev = &pdev->dev;
 struct pwm_ir *ir;
 struct rc_dev *rc;
 int ret;
 ir = devm_kzalloc(dev, sizeof(*ir), GFP_KERNEL);
 if (!ir)
  return -ENOMEM;
 mutex_init(&ir->lock);
 ir->carrier = 38000;
 ir->duty_cycle = 50;
 ir->clk = devm_clk_get(dev, "gpio-pwm-clk");
 if (IS_ERR(ir->clk))
  return PTR_ERR(ir->clk);
 ir->pins = devm_pinctrl_get(dev);
 if (IS_ERR(ir->pins))
  return PTR_ERR(ir->pins);
 ir->active = pinctrl_lookup_state(ir->pins, "ir_active");
 if (IS_ERR(ir->active))
  return PTR_ERR(ir->active);
 ir->sleep = pinctrl_lookup_state(ir->pins, "ir_sleep");
 if (IS_ERR(ir->sleep))
  return PTR_ERR(ir->sleep);
 rc = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW_TX);
 if (!rc)
  return -ENOMEM;
 ir->rc = rc;
 rc->priv = ir;
 rc->driver_name = "pwm-ir-tx";
 rc->device_name = "PWM IR Transmitter";
 rc->tx_ir = pwm_ir_tx;
 rc->s_tx_duty_cycle = pwm_ir_set_duty_cycle;
 rc->s_tx_carrier = pwm_ir_set_carrier;
 ret = devm_rc_register_device(dev, rc);
 if (ret)
  return ret;
 ret = device_create_file(&rc->dev, &dev_attr_transmit);
 if (ret)
  return ret;
 return devm_add_action_or_reset(dev, remove_transmit, rc);
}
static const struct of_device_id pwm_ir_of_match[] = {
 { .compatible = "pwm-ir-tx" }, { }
};
MODULE_DEVICE_TABLE(of, pwm_ir_of_match);
static struct platform_driver pwm_ir_driver = {
 .probe = pwm_ir_probe,
 .driver = { .name = "pwm-ir-tx", .of_match_table = pwm_ir_of_match }
};
module_platform_driver(pwm_ir_driver);
MODULE_DESCRIPTION("M2391 clock PWM IR transmitter");
MODULE_AUTHOR("Sean Young; M2391 source recovery");
MODULE_LICENSE("GPL");
