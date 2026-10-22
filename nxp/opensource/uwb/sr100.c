/*====================================================================================*/
/*                                                                                    */
/*                        Copyright 2018-2019 NXP                                     */
/*                                                                                    */
/* This program is free software; you can redistribute it and/or modify               */
/* it under the terms of the GNU General Public License as published by               */
/* the Free Software Foundation; either version 2 of the License, or                  */
/* (at your option) any later version.                                                */
/*                                                                                    */
/* This program is distributed in the hope that it will be useful,                    */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of                     */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                      */
/* GNU General Public License for more details.                                       */
/*                                                                                    */
/* You should have received a copy of the GNU General Public License                  */
/* along with this program; if not, write to the Free Software                        */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA          */
/*                                                                                    */
/*====================================================================================*/

/* NXP UCI/HBCI transport, with M2391 power/clock/ABI recovered from nxp-sr100.ko. */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/ipc_logging.h>
#include <linux/irq.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include "sr100.h"

#define SR100_BUFSIZE 4200
#define NORMAL_MODE_HEADER_LEN 4
#define MAX_UCI_PKT_SIZE (SR100_BUFSIZE - NORMAL_MODE_HEADER_LEN)
#define NORMAL_MODE_LEN_OFFSET 3
#define EXTENDED_LENGTH_OFFSET 2
#define EXTND_LEN_INDICATOR_OFFSET 1
#define EXTND_LEN_INDICATOR_OFFSET_MASK 0x80

enum spi_status_codes {
	spi_transcive_success, spi_transcive_fail,
	spi_irq_wait_request, spi_irq_wait_timeout,
};
enum spi_operation_modes { SR100_WRITE_MODE, SR100_READ_MODE };

static void *sr100_ctx, *sr100_uci_ctx;
#define sr100_dbg(fmt, ...) do { \
	if (sr100_ctx) ipc_log_string(sr100_ctx, "%-6d: " fmt, \
		__LINE__, ##__VA_ARGS__); \
} while (0)
#define sr100_info(fmt, ...) do { \
	sr100_dbg(fmt, ##__VA_ARGS__); \
	pr_info("[NXP-SR100]:%d " fmt, __LINE__, ##__VA_ARGS__); \
} while (0)
#define sr100_err(fmt, ...) do { \
	sr100_dbg(fmt, ##__VA_ARGS__); \
	pr_err("[NXP-SR100]:%d " fmt, __LINE__, ##__VA_ARGS__); \
} while (0)

struct sr100_dev {
	wait_queue_head_t read_wq;
	struct spi_device *spi;
	struct miscdevice sr100_device;
	int ce_gpio, irq_gpio, spi_handshake_gpio;
	unsigned int irq_enabled, irq_received;
	spinlock_t irq_enabled_lock;
	u8 *tx_buffer, *rx_buffer;
	unsigned int write_count, read_count;
	struct mutex sr100_access_lock;
	size_t totalBtyesToRead, IsExtndLenIndication;
	int mode;
	long timeOutInMs;
	struct regulator *vdd_reg, *rfvdd_reg;
	struct clk *refclk, *slpclk;
	u32 vdd_levels[2], vdd_max_load;
	u32 rfvdd_levels[2], rfvdd_max_load;
	bool vdd_enabled, rfvdd_enabled, refclk_enabled, slpclk_enabled;
	int rfclk_ctrl_mode, rfclk_req_gpio;
	struct kref ref;
	struct rw_semaphore operation_sem;
	struct mutex read_mutex, write_mutex, control_mutex;
	bool removing, irq_requested;
	bool read_abort_requested, is_fw_dwnld_enabled;
};

static void sr100_free(struct kref *ref)
{
	struct sr100_dev *d = container_of(ref, struct sr100_dev, ref);

	kfree(d->rx_buffer);
	kfree(d->tx_buffer);
	kfree(d);
}

static int sr100_begin(struct sr100_dev *d)
{
	down_read(&d->operation_sem);
	if (READ_ONCE(d->removing)) {
		up_read(&d->operation_sem);
		return -ENODEV;
	}
	return 0;
}

static int sr100_dev_open(struct inode *inode, struct file *file)
{
	struct sr100_dev *d = container_of(file->private_data,
					 struct sr100_dev, sr100_device);
	int rc = sr100_begin(d);

	if (rc)
		return rc;
	kref_get(&d->ref);
	file->private_data = d;
	up_read(&d->operation_sem);
	return 0;
}

static int sr100_dev_release(struct inode *inode, struct file *file)
{
	struct sr100_dev *d = file->private_data;

	kref_put(&d->ref, sr100_free);
	return 0;
}

static void sr100_disable_irq(struct sr100_dev *d)
{
	unsigned long flags;

	spin_lock_irqsave(&d->irq_enabled_lock, flags);
	if (d->irq_enabled) {
		disable_irq_nosync(d->spi->irq);
		WRITE_ONCE(d->irq_received, 1);
		d->irq_enabled = 0;
	}
	spin_unlock_irqrestore(&d->irq_enabled_lock, flags);
}

static void sr100_enable_irq(struct sr100_dev *d)
{
	unsigned long flags;

	spin_lock_irqsave(&d->irq_enabled_lock, flags);
	if (!d->irq_enabled && !READ_ONCE(d->removing)) {
		WRITE_ONCE(d->irq_received, 0);
		d->irq_enabled = 1;
		enable_irq(d->spi->irq);
	}
	spin_unlock_irqrestore(&d->irq_enabled_lock, flags);
}

static irqreturn_t sr100_dev_irq_handler(int irq, void *data)
{
	struct sr100_dev *d = data;

	sr100_disable_irq(d);
	wake_up_interruptible(&d->read_wq);
	return IRQ_HANDLED;
}

static int sr100_set_clock(struct clk *clock, bool *running, bool enable)
{
	int rc;

	if (!clock)
		return 0;
	if (*running == enable) {
		sr100_err("clock already %s\n", enable ? "enabled" : "disabled");
		return 0;
	}
	if (enable) {
		rc = clk_prepare(clock);
		if (rc)
			return rc;
		rc = clk_enable(clock);
		if (rc) {
			clk_unprepare(clock);
			return rc;
		}
	} else {
		clk_disable(clock);
		clk_unprepare(clock);
	}
	*running = enable;
	sr100_dbg("clock %s\n", enable ? "enabled" : "disabled");
	return 0;
}

static int sr100_set_refclk(struct sr100_dev *d, bool enable)
{
	if (d->rfclk_ctrl_mode == 1) {
		sr100_dbg("ignored refclk control: %d\n", enable);
		return 0;
	}
	return sr100_set_clock(d->refclk, &d->refclk_enabled, enable);
}

static int sr100_set_slpclk(struct sr100_dev *d, bool enable)
{
	return sr100_set_clock(d->slpclk, &d->slpclk_enabled, enable);
}

/* The original dumps 13 bytes per 40-byte formatted line. */
static void sr100_log_uci(const char *direction, const char *stage,
			    const u8 *buffer, unsigned int count)
{
	unsigned int pos, n, i, used;
	char line[40];

	if (!sr100_uci_ctx)
		return;
	for (pos = 0; pos < count; pos += n) {
		n = min(count - pos, 13U);
		used = 0;
		for (i = 0; i < n; i++)
			used += scnprintf(line + used, sizeof(line) - used,
					  "%02x ", buffer[pos + i]);
		ipc_log_string(sr100_uci_ctx, "%s %02x %s: %s\n",
			       direction, 0, stage, line);
	}
}

static long sr100_dev_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct sr100_dev *d = file->private_data;
	int rc = sr100_begin(d), clock_rc;

	if (rc)
		return rc;
	sr100_dbg("ioctl cmd=%x arg=%lu\n", cmd, arg);
	mutex_lock(&d->control_mutex);
	switch (cmd) {
	case SR100_SET_PWR:
		if (arg == 1) {
			clock_rc = sr100_set_slpclk(d, true);
			if (clock_rc)
				sr100_err("failed to enable slpclk: %d\n", clock_rc);
			clock_rc = sr100_set_refclk(d, true);
			if (clock_rc)
				sr100_err("failed to enable refclk: %d\n", clock_rc);
			gpio_set_value(d->ce_gpio, 1);
			msleep(10);
		} else if (arg == 0) {
			sr100_disable_irq(d);
			gpio_set_value(d->ce_gpio, 0);
			sr100_set_slpclk(d, false);
			sr100_set_refclk(d, false);
			msleep(10);
		} else if (arg == 2) {
			WRITE_ONCE(d->read_abort_requested, true);
			sr100_disable_irq(d);
			wake_up_interruptible(&d->read_wq);
		}
		break;
	case SR100_SET_FWD:
		if (arg <= 1) {
			WRITE_ONCE(d->is_fw_dwnld_enabled, !!arg);
			if (arg)
				WRITE_ONCE(d->read_abort_requested, false);
			sr100_info("FW download %s.\n", arg ? "enabled" : "disabled");
		}
		break;
	case SR100_COMPAT_NOOP:
		/* Recognized and deliberately does nothing in the shipping driver. */
		break;
	default:
		sr100_err("unknown ioctl: %x\n", cmd);
		break;
	}
	mutex_unlock(&d->control_mutex);
	up_read(&d->operation_sem);
	return 0;
}

/* NXP's two-interrupt UCI handshake. Keep both header and payload transfers. */
static int sr100_dev_transceive(struct sr100_dev *d, int mode, int count)
{
	int rc = -EIO, retry;

	mutex_lock(&d->sr100_access_lock);
	d->mode = mode;
	d->totalBtyesToRead = 0;
	d->IsExtndLenIndication = 0;
	if (READ_ONCE(d->removing)) {
		rc = -ENODEV;
		goto out;
	}
	if (mode == SR100_WRITE_MODE) {
		d->write_count = 0;
		rc = spi_write(d->spi, d->tx_buffer, NORMAL_MODE_HEADER_LEN);
		sr100_log_uci("UCIX", "HDR", d->tx_buffer, NORMAL_MODE_HEADER_LEN);
		if (rc < 0) {
			sr100_err("spi_write header: Failed %d\n", rc);
			rc = -EIO;
			goto out;
		}
		count -= NORMAL_MODE_HEADER_LEN;
		if (count > 0) {
			udelay(10);
			rc = spi_write(d->spi, d->tx_buffer + NORMAL_MODE_HEADER_LEN,
				       count);
			sr100_log_uci("UCIX", "PYD",
				      d->tx_buffer + NORMAL_MODE_HEADER_LEN, count);
			if (rc < 0) {
				rc = -EIO;
				goto out;
			}
		}
		d->write_count = count + NORMAL_MODE_HEADER_LEN;
		rc = spi_transcive_success;
		goto out;
	}
	if (!gpio_get_value(d->irq_gpio)) {
		rc = spi_irq_wait_request;
		goto out;
	}
	gpio_set_value(d->spi_handshake_gpio, 1);
	for (retry = 0; gpio_get_value(d->irq_gpio) && retry < 100; retry++)
		udelay(10);
	sr100_enable_irq(d);
	d->read_count = 0;
	rc = wait_event_interruptible_timeout(d->read_wq,
		READ_ONCE(d->irq_received) || READ_ONCE(d->removing) ||
		READ_ONCE(d->read_abort_requested), d->timeOutInMs);
	if (READ_ONCE(d->removing)) {
		rc = -ENODEV;
		goto out;
	}
	if (rc < 0)
		goto out;
	if (READ_ONCE(d->read_abort_requested)) {
		rc = -ECANCELED;
		goto out;
	}
	if (!rc) {
		sr100_err("wait_event_interruptible timeout: Failed\n");
		rc = spi_irq_wait_timeout;
		goto out;
	}
	if (!gpio_get_value(d->irq_gpio)) {
		sr100_err("Second IRQ is Low\n");
		rc = -EIO;
		goto out;
	}
	rc = spi_read(d->spi, d->rx_buffer, NORMAL_MODE_HEADER_LEN);
	sr100_log_uci("UCIR", "HDR", d->rx_buffer, NORMAL_MODE_HEADER_LEN);
	if (rc < 0)
		goto out;
	d->IsExtndLenIndication = d->rx_buffer[EXTND_LEN_INDICATOR_OFFSET] &
		EXTND_LEN_INDICATOR_OFFSET_MASK;
	d->totalBtyesToRead = d->rx_buffer[NORMAL_MODE_LEN_OFFSET];
	/* DATA packets (MT=0) always encode the 16-bit little-endian length. */
	if (d->rx_buffer[0] <= 0x1f || d->IsExtndLenIndication)
		d->totalBtyesToRead = (d->totalBtyesToRead << 8) |
			d->rx_buffer[EXTENDED_LENGTH_OFFSET];
	if (d->totalBtyesToRead > MAX_UCI_PKT_SIZE) {
		sr100_err("Length %zu exceeds max limit %d\n",
			  d->totalBtyesToRead, MAX_UCI_PKT_SIZE);
		rc = -EIO;
		goto out;
	}
	if (d->totalBtyesToRead) {
		rc = spi_read(d->spi, d->rx_buffer + NORMAL_MODE_HEADER_LEN,
			      d->totalBtyesToRead);
		sr100_log_uci("UCIR", "PYD", d->rx_buffer + NORMAL_MODE_HEADER_LEN,
			      d->totalBtyesToRead);
		if (rc < 0)
			goto out;
	}
	d->read_count = d->totalBtyesToRead + NORMAL_MODE_HEADER_LEN;
	for (retry = 0; retry < 1000; retry++) {
		usleep_range(10, 15);
		if (!gpio_get_value(d->irq_gpio) || READ_ONCE(d->removing))
			break;
	}
	if (retry == 1000)
		sr100_err("Slave not released the IRQ even after 1ms\n");
	rc = spi_transcive_success;
out:
	if (mode == SR100_READ_MODE)
		gpio_set_value(d->spi_handshake_gpio, 0);
	mutex_unlock(&d->sr100_access_lock);
	return rc;
}

static ssize_t sr100_dev_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *offset)
{
	struct sr100_dev *d = file->private_data;
	bool firmware;
	int rc = sr100_begin(d);

	if (rc)
		return rc;
	mutex_lock(&d->write_mutex);
	firmware = READ_ONCE(d->is_fw_dwnld_enabled);
	if (count > SR100_BUFSIZE) {
		rc = -ENOBUFS;
		goto out;
	}
	if (!count) {
		rc = 0;
		goto out;
	}
	if (!firmware && count < NORMAL_MODE_HEADER_LEN) {
		rc = -EINVAL;
		goto out;
	}
	if (copy_from_user(d->tx_buffer, buffer, count)) {
		rc = -EFAULT;
		goto out;
	}
	if (firmware) {
		mutex_lock(&d->sr100_access_lock);
		rc = spi_write(d->spi, d->tx_buffer, count);
		mutex_unlock(&d->sr100_access_lock);
		if (rc < 0)
			rc = -EIO;
		else {
			d->write_count = count;
			sr100_enable_irq(d);
			rc = count;
		}
	} else {
		rc = sr100_dev_transceive(d, SR100_WRITE_MODE, count);
		if (rc == spi_transcive_success)
			rc = d->write_count;
	}
out:
	mutex_unlock(&d->write_mutex);
	up_read(&d->operation_sem);
	return rc;
}

static ssize_t sr100_dev_read(struct file *file, char __user *buffer,
			     size_t count, loff_t *offset)
{
	struct sr100_dev *d = file->private_data;
	bool firmware;
	int rc = sr100_begin(d);

	if (rc)
		return rc;
	mutex_lock(&d->read_mutex);
	if (READ_ONCE(d->removing)) {
		rc = -ENODEV;
		goto out;
	}
	if (!count) {
		rc = 0;
		goto out;
	}
	firmware = READ_ONCE(d->is_fw_dwnld_enabled);
	d->timeOutInMs = msecs_to_jiffies(500);
	memset(d->rx_buffer, 0, SR100_BUFSIZE);
	if (!gpio_get_value(d->irq_gpio) && (file->f_flags & O_NONBLOCK)) {
		rc = -EAGAIN;
		goto out;
	}
	if (firmware) {
		if (count > SR100_BUFSIZE) {
			rc = -EINVAL;
			goto out;
		}
		rc = wait_event_interruptible_timeout(d->read_wq,
			READ_ONCE(d->irq_received) || READ_ONCE(d->removing) ||
			READ_ONCE(d->read_abort_requested), d->timeOutInMs);
		if (READ_ONCE(d->removing)) {
			rc = -ENODEV;
			goto out;
		}
		if (rc < 0)
			goto out;
		if (READ_ONCE(d->read_abort_requested))
			goto abort;
		if (!rc || !gpio_get_value(d->irq_gpio)) {
			rc = -EIO;
			goto out;
		}
		mutex_lock(&d->sr100_access_lock);
		rc = spi_read(d->spi, d->rx_buffer, count);
		mutex_unlock(&d->sr100_access_lock);
		if (rc < 0)
			goto out;
		d->read_count = count;
	} else {
again:
		sr100_enable_irq(d);
		rc = wait_event_interruptible(d->read_wq,
			READ_ONCE(d->irq_received) || READ_ONCE(d->removing) ||
			READ_ONCE(d->read_abort_requested));
		if (READ_ONCE(d->removing)) {
			rc = -ENODEV;
			goto out;
		}
		if (rc < 0)
			goto out;
		if (READ_ONCE(d->read_abort_requested))
			goto abort;
		rc = sr100_dev_transceive(d, SR100_READ_MODE, count);
		if (rc == spi_irq_wait_request)
			goto again;
		if (rc == -ECANCELED)
			goto abort;
		if (rc != spi_transcive_success) {
			if (rc >= 0)
				rc = -EIO;
			goto out;
		}
	}
	if (d->read_count > count) {
		rc = -EMSGSIZE;
		goto out;
	}
	rc = copy_to_user(buffer, d->rx_buffer, d->read_count) ?
		-EFAULT : d->read_count;
	goto out;
abort:
	WRITE_ONCE(d->read_abort_requested, false);
	rc = 0;
out:
	mutex_unlock(&d->read_mutex);
	up_read(&d->operation_sem);
	return rc;
}

static const struct file_operations sr100_dev_fops = {
	.owner = THIS_MODULE,
	.read = sr100_dev_read,
	.write = sr100_dev_write,
	.open = sr100_dev_open,
	.release = sr100_dev_release,
	.unlocked_ioctl = sr100_dev_ioctl,
};

/* Shipping RF supply properties intentionally reuse the VDD voltage/load keys. */
static int sr100_supply(struct sr100_dev *d, const char *name,
		       struct regulator **reg, u32 levels[2], u32 *load)
{
	struct device *dev = &d->spi->dev;
	char property[64];
	int rc;

	snprintf(property, sizeof(property), "%s-supply", name);
	if (!of_find_property(dev->of_node, property, NULL))
		return 0;
	*reg = devm_regulator_get(dev, name);
	if (IS_ERR(*reg)) {
		rc = PTR_ERR(*reg);
		*reg = NULL;
		return rc;
	}
	rc = of_property_read_u32_array(dev->of_node,
			"nxp,sr100-vdd-1p8-voltage", levels, 2);
	if (rc)
		return rc;
	return of_property_read_u32(dev->of_node,
			"nxp,sr100-vdd-1p8-current", load);
}

static int sr100_parse_dt(struct sr100_dev *d)
{
	struct device *dev = &d->spi->dev;
	struct device_node *np = dev->of_node;
	const char *mode;
	int rc;

	d->irq_gpio = of_get_named_gpio(np, "nxp,sr100-irq", 0);
	if (d->irq_gpio < 0)
		return d->irq_gpio;
	d->ce_gpio = of_get_named_gpio(np, "nxp,sr100-ce", 0);
	if (d->ce_gpio < 0)
		return d->ce_gpio;
	d->spi_handshake_gpio = of_get_named_gpio(np, "nxp,sr100-ri", 0);
	if (d->spi_handshake_gpio < 0)
		return d->spi_handshake_gpio;
	rc = sr100_supply(d, "nxp,sr100-vdd-1p8", &d->vdd_reg,
			  d->vdd_levels, &d->vdd_max_load);
	if (rc)
		return rc;
	rc = sr100_supply(d, "nxp,sr100-rfvdd-1p8", &d->rfvdd_reg,
			  d->rfvdd_levels, &d->rfvdd_max_load);
	if (rc)
		return rc;
	if (!of_property_read_string(np, "nxp,sr100-rfclk-ctrl-mode", &mode)) {
		if (!strcmp(mode, "auto"))
			d->rfclk_ctrl_mode = 0;
		else if (!strcmp(mode, "none"))
			d->rfclk_ctrl_mode = 1;
		else if (!strcmp(mode, "hw"))
			d->rfclk_ctrl_mode = 2;
		else if (!strcmp(mode, "irq"))
			d->rfclk_ctrl_mode = 3;
		else
			return -EINVAL;
	}
	d->refclk = devm_clk_get(dev, "uwb_refclk");
	if (IS_ERR(d->refclk)) {
		rc = PTR_ERR(d->refclk);
		d->refclk = NULL;
		if (rc == -EPROBE_DEFER || d->rfclk_ctrl_mode == 0 ||
		    d->rfclk_ctrl_mode == 3)
			return rc;
	}
	d->rfclk_req_gpio = of_get_named_gpio(np, "nxp,sr100-rfclk-req", 0);
	if (d->rfclk_req_gpio == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (d->rfclk_ctrl_mode == 3 && !gpio_is_valid(d->rfclk_req_gpio))
		return d->rfclk_req_gpio < 0 ? d->rfclk_req_gpio : -EINVAL;
	d->slpclk = devm_clk_get(dev, "sleep_clk");
	if (IS_ERR(d->slpclk)) {
		rc = PTR_ERR(d->slpclk);
		d->slpclk = NULL;
		return rc;
	}
	return 0;
}

static int sr100_enable_supply(struct regulator *reg, const u32 levels[2],
			       u32 load, bool *enabled)
{
	int rc;

	if (!reg)
		return 0;
	rc = regulator_set_voltage(reg, levels[0], levels[1]);
	if (rc < 0)
		return rc;
	rc = regulator_set_load(reg, load);
	if (rc < 0)
		return rc;
	rc = regulator_enable(reg);
	if (rc < 0)
		return rc;
	*enabled = true;
	return 0;
}

static void sr100_power_down(struct sr100_dev *d)
{
	if (d->rfvdd_enabled) {
		regulator_disable(d->rfvdd_reg);
		d->rfvdd_enabled = false;
	}
	if (d->vdd_enabled) {
		regulator_disable(d->vdd_reg);
		d->vdd_enabled = false;
	}
	sr100_set_refclk(d, false);
	sr100_set_slpclk(d, false);
}

static int sr100_probe(struct spi_device *spi)
{
	struct sr100_dev *d;
	int rc;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->spi = spi;
	sr100_dbg("probe chip_select=%d bus_num=%d\n",
		    spi->chip_select, spi->master->bus_num);
	kref_init(&d->ref);
	init_rwsem(&d->operation_sem);
	init_waitqueue_head(&d->read_wq);
	spin_lock_init(&d->irq_enabled_lock);
	mutex_init(&d->sr100_access_lock);
	mutex_init(&d->read_mutex);
	mutex_init(&d->write_mutex);
	mutex_init(&d->control_mutex);
	d->tx_buffer = kzalloc(SR100_BUFSIZE, GFP_KERNEL);
	d->rx_buffer = kzalloc(SR100_BUFSIZE, GFP_KERNEL);
	if (!d->tx_buffer || !d->rx_buffer) {
		rc = -ENOMEM;
		goto fail;
	}
	rc = sr100_parse_dt(d);
	if (rc)
		goto fail;
	rc = devm_gpio_request_one(&spi->dev, d->irq_gpio, GPIOF_IN,
				   "sr100_irq");
	if (rc)
		goto fail;
	rc = devm_gpio_request_one(&spi->dev, d->ce_gpio, GPIOF_OUT_INIT_LOW,
				   "sr100_ce");
	if (rc)
		goto fail;
	rc = devm_gpio_request_one(&spi->dev, d->spi_handshake_gpio,
				   GPIOF_OUT_INIT_LOW, "sr100_ri");
	if (rc)
		goto fail;
	if (gpio_is_valid(d->rfclk_req_gpio)) {
		rc = devm_gpio_request_one(&spi->dev, d->rfclk_req_gpio, GPIOF_IN,
					   "sr100_rfclk_req");
		if (rc)
			goto fail;
	}
	spi->irq = gpio_to_irq(d->irq_gpio);
	if (spi->irq < 0) {
		rc = spi->irq;
		goto fail;
	}
	irq_set_status_flags(spi->irq, IRQ_NOAUTOEN);
	rc = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
				 sr100_dev_irq_handler, IRQF_TRIGGER_RISING,
				 "srxxx", d);
	if (rc)
		goto fail;
	d->irq_requested = true;
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi->max_speed_hz = 16000000;
	rc = spi_setup(spi);
	if (rc)
		goto fail;
	rc = sr100_enable_supply(d->vdd_reg, d->vdd_levels,
				 d->vdd_max_load, &d->vdd_enabled);
	if (rc)
		goto fail;
	rc = sr100_enable_supply(d->rfvdd_reg, d->rfvdd_levels,
				 d->rfvdd_max_load, &d->rfvdd_enabled);
	if (rc)
		goto fail;
	d->sr100_device.minor = MISC_DYNAMIC_MINOR;
	d->sr100_device.name = "srxxx";
	d->sr100_device.fops = &sr100_dev_fops;
	d->sr100_device.parent = &spi->dev;
	spi_set_drvdata(spi, d);
	rc = misc_register(&d->sr100_device);
	if (!rc)
		return 0;
	spi_set_drvdata(spi, NULL);
fail:
	WRITE_ONCE(d->removing, true);
	wake_up_interruptible(&d->read_wq);
	if (d->irq_requested)
		devm_free_irq(&spi->dev, spi->irq, d);
	sr100_power_down(d);
	kref_put(&d->ref, sr100_free);
	return rc;
}

static int sr100_remove(struct spi_device *spi)
{
	struct sr100_dev *d = spi_get_drvdata(spi);

	WRITE_ONCE(d->removing, true);
	wake_up_interruptible(&d->read_wq);
	/* misc core serializes open against deregister; owner ref stays alive. */
	misc_deregister(&d->sr100_device);
	down_write(&d->operation_sem);
	sr100_disable_irq(d);
	devm_free_irq(&spi->dev, spi->irq, d);
	sr100_power_down(d);
	spi_set_drvdata(spi, NULL);
	up_write(&d->operation_sem);
	kref_put(&d->ref, sr100_free);
	return 0;
}

static const struct of_device_id sr100_dt_match[] = {
	{ .compatible = "nxp,sr100" }, { }
};
MODULE_DEVICE_TABLE(of, sr100_dt_match);

static struct spi_driver sr100_driver = {
	.driver = {
		.name = "sr100",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
		.of_match_table = sr100_dt_match,
	},
	.probe = sr100_probe,
	.remove = sr100_remove,
};

static int __init sr100_dev_init(void)
{
	int rc;

	sr100_ctx = ipc_log_context_create(10, "nq-uwb", 0);
	sr100_uci_ctx = ipc_log_context_create(10, "nq-uwb_uci", 0);
	rc = spi_register_driver(&sr100_driver);
	if (rc) {
		if (sr100_uci_ctx)
			ipc_log_context_destroy(sr100_uci_ctx);
		if (sr100_ctx)
			ipc_log_context_destroy(sr100_ctx);
	}
	return rc;
}
module_init(sr100_dev_init);

static void __exit sr100_dev_exit(void)
{
	spi_unregister_driver(&sr100_driver);
	if (sr100_uci_ctx)
		ipc_log_context_destroy(sr100_uci_ctx);
	if (sr100_ctx)
		ipc_log_context_destroy(sr100_ctx);
}
module_exit(sr100_dev_exit);
MODULE_AUTHOR("Manjunatha Venkatesh");
MODULE_DESCRIPTION("NXP SR100 SPI driver");
MODULE_LICENSE("GPL");
