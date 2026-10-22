// SPDX-License-Identifier: GPL-2.0-only
/* M2391 GN5 OIS controls recovered from original camera.ko instructions.
 * Register addresses, transfer widths, state checks and response text are
 * preserved; callbacks serialize against the camera driver's existing mutex.
 */
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <asm/unaligned.h>
#include <linux/swab.h>
#include "cam_ois_meizu.h"
#include "cam_ois_meizu_firmware.h"
#include "../cam_eeprom/cam_eeprom_meizu.h"
#include "cam_debug_util.h"
#include "../../../../../../meizu/system/meizu_sys.h"
#include "../../../../../../meizu/storage/meizu_storage.h"

static DEFINE_MUTEX(meizu_ois_registry_lock);
static struct cam_ois_ctrl_t *meizu_primary_ois;

static int meizu_ois_write(struct cam_ois_ctrl_t *ctrl, u32 address, u32 value,
		enum camera_sensor_i2c_type width)
{
	struct cam_sensor_i2c_reg_array reg = {
		.reg_addr = address, .reg_data = value,
	};
	struct cam_sensor_i2c_reg_setting setting = {
		.reg_setting = &reg, .size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD, .data_type = width,
	};

	return camera_io_dev_write(&ctrl->io_master_info, &setting);
}

static int meizu_ois_read(struct cam_ois_ctrl_t *ctrl, u32 address, u32 *value,
		enum camera_sensor_i2c_type width, bool probing)
{
	return camera_io_dev_read(&ctrl->io_master_info, address, value,
		CAMERA_SENSOR_I2C_TYPE_WORD, width, probing);
}

/* Stock .text 0x1768e0: 0x05 means off, 0x10 on, other values retain status. */
static ssize_t ois_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 value = 0;
	ssize_t length;

	if (!ctrl)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	meizu_ois_read(ctrl, 2, &value, CAMERA_SENSOR_I2C_TYPE_BYTE, true);
	if (value == 5)
		ctrl->meizu_enable_status = 0;
	else if (value == 16)
		ctrl->meizu_enable_status = 1;
	length = sysfs_emit(buf, "%d\n", ctrl->meizu_enable_status);
	mutex_unlock(&ctrl->ois_mutex);
	return length;
}

static ssize_t ois_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	int enable = 0, rc;

	if (!ctrl)
		return -EINVAL;
	if (sscanf(buf, "%d", &enable) != 1)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state != CAM_OIS_START) {
		mutex_unlock(&ctrl->ois_mutex);
		return -EIO;
	}
	rc = meizu_ois_write(ctrl, 2, enable ? 16 : 5, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "Meizu OIS enable write failed: %d", rc);
	/* Stock records the requested state even after a write error. */
	ctrl->meizu_enable_status = !!enable;
	mutex_unlock(&ctrl->ois_mutex);
	return count;
}

/* Stock .text 0x173888: flags 1/2/4 select reads, 0/3/5 issue writes. */
static ssize_t reg_data_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 address = 0, value = 0;
	int flag = 0, rc;
	enum camera_sensor_i2c_type width;

	if (!ctrl)
		return -EINVAL;
	if (sscanf(buf, "0x%x 0x%x %d", &address, &value, &flag) != 3)
		return -EINVAL;
	flag = (u8)flag;
	mutex_lock(&ctrl->ois_mutex);
	if (flag == 1 || flag == 2 || flag == 4) {
		ctrl->meizu_read_width = flag;
	} else {
		switch (flag) {
		case 0: width = CAMERA_SENSOR_I2C_TYPE_BYTE; break;
		case 3: width = CAMERA_SENSOR_I2C_TYPE_WORD; break;
		case 5: width = CAMERA_SENSOR_I2C_TYPE_DWORD; break;
		default:
			mutex_unlock(&ctrl->ois_mutex);
			return -EINVAL;
		}
		rc = meizu_ois_write(ctrl, address, value, width);
		if (rc < 0)
			CAM_ERR(CAM_OIS, "Meizu register write failed: %d", rc);
	}
	ctrl->meizu_read_address = address;
	mutex_unlock(&ctrl->ois_mutex);
	return count;
}

static ssize_t reg_data_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 value = 0, swapped;
	ssize_t length;
	int rc;

	if (!ctrl)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	rc = meizu_ois_read(ctrl, ctrl->meizu_read_address, &value,
		ctrl->meizu_read_width, false);
	if (rc < 0) {
		length = sysfs_emit(buf, "read fail. reg_addr:0x%x.\n",
			ctrl->meizu_read_address);
	} else {
		if (ctrl->meizu_read_width == CAMERA_SENSOR_I2C_TYPE_WORD)
			value = swab16(value);
		swapped = ctrl->meizu_read_width == CAMERA_SENSOR_I2C_TYPE_DWORD ?
			swab32(value) : 0;
		length = sysfs_emit(buf,
			"read success. reg_addr:0x%x, data:0x%x, 0x%x \n",
			ctrl->meizu_read_address, value, swapped);
	}
	mutex_unlock(&ctrl->ois_mutex);
	return length;
}

/* Stock ois_xy_show first enables monitor 0x80, checks status 0/2, then
 * reads gyro words 0x82/0x84. It has intentionally distinct -1/-2/-3 errors.
 */
static ssize_t ois_xy_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 value = 0;
	ssize_t length;
	int rc;

	if (!ctrl)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state <= CAM_OIS_ACQUIRE) {
		length = sysfs_emit(buf, "Not in right state(%d) to control OIS\n",
			ctrl->cam_ois_state);
		goto out;
	}
	rc = meizu_ois_write(ctrl, 0x80, 1, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) { length = -1; goto out; }
	rc = meizu_ois_read(ctrl, 0, &value, CAMERA_SENSOR_I2C_TYPE_BYTE, true);
	if (rc < 0) { length = -2; goto out; }
	if (value != 1) { length = -3; goto out; }
	rc = meizu_ois_read(ctrl, 2, &value, CAMERA_SENSOR_I2C_TYPE_BYTE, true);
	if (rc < 0) { length = -1; goto out; }
	if (value != 16 && value != 19) { length = -3; goto out; }
	rc = meizu_ois_read(ctrl, 0x82, &value, CAMERA_SENSOR_I2C_TYPE_WORD, true);
	if (rc < 0)
		length = sysfs_emit(buf, "gyro_data_x, reg:0x0082, i2c read error, rc:%d\n", rc);
	else
		length = sysfs_emit(buf, "gyro_data_x, reg:0x0082, reg_val:0x%x\n", value);
	rc = meizu_ois_read(ctrl, 0x84, &value, CAMERA_SENSOR_I2C_TYPE_WORD, true);
	if (rc < 0)
		length += sysfs_emit_at(buf, length,
			"gyro_data_y, reg:0x0084, i2c read error, rc:%d\n", rc);
	else
		length += sysfs_emit_at(buf, length,
			"gyro_data_y, reg:0x0084, reg_val:0x%x\n", value);
out:
	mutex_unlock(&ctrl->ois_mutex);
	return length;
}

/* The original ois_xy_store is a literal return-count callback. */
static ssize_t ois_xy_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}

/* Stock 0x178388..0x1789b0: preserve 24 tuning bytes across reset.
 * The original uses 1 ms busy waits (10 per byte, 7000/2000 at reset).
 * This process-context interface sleeps for the same minimum intervals.
 */
static ssize_t ois_reset_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	int result;

	mutex_lock(&ctrl->ois_mutex);
	result = ctrl->meizu_cali_result;
	mutex_unlock(&ctrl->ois_mutex);
	return sysfs_emit(buf, "%d \n", result);
}

static ssize_t ois_reset_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 saved[24] = { 0 }, status[3] = { 0 };
	int request = 0, i, rc, total = 0;
	ssize_t result = count;

	if (sscanf(buf, "%d", &request) != 1 || request != 1)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state != CAM_OIS_START) {
		ctrl->meizu_cali_result = -1;
		result = -EIO;
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(saved); i++) {
		rc = meizu_ois_read(ctrl, 0x300 + i, &saved[i],
			CAMERA_SENSOR_I2C_TYPE_BYTE, true);
		total += rc;
		if (total < 0) {
			ctrl->meizu_cali_result = -2;
			goto out;
		}
		msleep(10);
	}
	for (i = 0; i < ARRAY_SIZE(status); i++)
		meizu_ois_read(ctrl, 0x240 + i, &status[i],
			CAMERA_SENSOR_I2C_TYPE_BYTE, true);
	if (status[0] != 0 || status[1] != 1 || status[2] != 1) {
		ctrl->meizu_cali_result = -3;
		goto out;
	}
	rc = meizu_ois_write(ctrl, 0x36, 0x81, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		ctrl->meizu_cali_result = -4;
		goto out;
	}
	msleep(7000);
	rc = meizu_ois_write(ctrl, 0x03, 0x01, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0) {
		ctrl->meizu_cali_result = -5;
		goto out;
	}
	msleep(2000);
	for (i = 0; i < ARRAY_SIZE(saved); i++) {
		rc = meizu_ois_write(ctrl, 0x300 + i, saved[i],
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		if (rc < 0) {
			ctrl->meizu_cali_result = -6;
			goto out;
		}
		msleep(10);
	}
	msleep(2000);
	ctrl->meizu_cali_result = 0;
out:
	mutex_unlock(&ctrl->ois_mutex);
	return result;
}

/* Stock 0x17719c: input is unsigned range [0, 19], despite show text.
 * Keep its 32-bit multiply overflow and 16-bit extraction; AArch64 UDIV
 * yields zero for a zero divisor, handled explicitly here for C semantics.
 */
static ssize_t ois_angle_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "adj_angle 0~2 \n");
}

static ssize_t ois_angle_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 x = 0, y = 0, base = 0, scale, value;
	u64 ratio;
	int angle = 0, rc;

	if (sscanf(buf, "%d", &angle) != 1 || (u32)angle >= 20)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	if (meizu_ois_read(ctrl, 0x8438, &x, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0 ||
	    meizu_ois_read(ctrl, 0x8447, &y, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0 ||
	    meizu_ois_read(ctrl, 0x8477, &base, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0)
		goto out;
	scale = ((u32)angle * base / 10) & 0xffff;
	ratio = scale ? ((u64)x << 14) / scale : 0;
	value = ratio > 32766 ? 32767 : (u16)ratio;
	rc = meizu_ois_write(ctrl, 0x8438, value, CAMERA_SENSOR_I2C_TYPE_WORD);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS angle X write failed %d", rc);
	rc = meizu_ois_write(ctrl, 0x84b8, value, CAMERA_SENSOR_I2C_TYPE_WORD);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS angle X mirror write failed %d", rc);
	value = ((u32)(y * scale) >> 14) & 0xffff;
	rc = meizu_ois_write(ctrl, 0x8447, value, CAMERA_SENSOR_I2C_TYPE_WORD);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS angle Y write failed %d", rc);
	rc = meizu_ois_write(ctrl, 0x84c7, value, CAMERA_SENSOR_I2C_TYPE_WORD);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS angle Y mirror write failed %d", rc);
out:
	mutex_unlock(&ctrl->ois_mutex);
	return count;
}

/* Stock sanity_show inlines Oscillation_Check. Its read has the original
 * persisted pass/fail byte side effect at main-camera payload offset 7265.
 */
static ssize_t sanity_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 x = 0, y = 0;
	int i, errors = 0, result = 0, rc;
	u8 persisted;

	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state != CAM_OIS_START) {
		mutex_unlock(&ctrl->ois_mutex);
		return -EIO;
	}
	rc = meizu_ois_write(ctrl, 0, 1, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if (rc < 0)
		CAM_ERR(CAM_OIS, "OIS oscillation start failed %d", rc);
	for (i = 0; i < ctrl->meizu_sanity_count; i++) {
		result = meizu_ois_read(ctrl, 0x843f, &x,
			CAMERA_SENSOR_I2C_TYPE_WORD, true);
		if (result < 0)
			goto out;
		result = meizu_ois_read(ctrl, 0x84bf, &y,
			CAMERA_SENSOR_I2C_TYPE_WORD, true);
		if (result < 0)
			goto out;
		if (abs((int)(s16)x) > ctrl->meizu_sanity_criterion ||
		    abs((int)(s16)y) > ctrl->meizu_sanity_criterion)
			errors++;
		mdelay(5);
	}
	persisted = errors > ctrl->meizu_sanity_level;
	if (persisted) {
		rc = meizu_ois_write(ctrl, 0x847f, 0,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		if (rc < 0)
			CAM_ERR(CAM_OIS, "OIS oscillation failure write failed %d", rc);
	}
	meizu_bsp_partition_write(&persisted, 1, 7265, 0);
	result = persisted ? -1 : 0;
out:
	mutex_unlock(&ctrl->ois_mutex);
	return sysfs_emit(buf, "%d\n", result);
}

static ssize_t sanity_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	int criterion, samples, level;

	if (sscanf(buf, "%d%d%d", &criterion, &samples, &level) != 3)
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	ctrl->meizu_sanity_criterion = criterion;
	ctrl->meizu_sanity_count = samples;
	ctrl->meizu_sanity_level = level;
	mutex_unlock(&ctrl->ois_mutex);
	return count;
}

/* Stock calibration execution is first-character triggered, and reports
 * execution errors through cali_exec_result while consuming the sysfs write.
 */
static ssize_t cali_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u8 *data;
	int rc, result;

	data = vzalloc(513);
	if (!data)
		return -ENOMEM;
	rc = sd_partition_rw("camera", 0, 256000, data, 512);
	mutex_lock(&ctrl->ois_mutex);
	if (!rc)
		ctrl->meizu_cali_result = data[44];
	result = ctrl->meizu_cali_result;
	mutex_unlock(&ctrl->ois_mutex);
	vfree(data);
	return rc ? rc : sysfs_emit(buf, "%d\n", result);
}

static ssize_t cali_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 value = 0;
	u8 persisted;
	int i, rc;

	if (!count || buf[0] != '1')
		return -EINVAL;
	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state != CAM_OIS_START) {
		ctrl->meizu_cali_result = -2;
		goto out;
	}
	if (meizu_ois_write(ctrl, 0, 0, CAMERA_SENSOR_I2C_TYPE_BYTE) < 0)
		ctrl->meizu_cali_result = -3;
	msleep(100);
	if (meizu_ois_read(ctrl, 1, &value, CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0) {
		ctrl->meizu_cali_result = -4;
		goto out;
	}
	/* The stock logs idle != 1, but still starts GCCTRL. */
	if (meizu_ois_write(ctrl, 0x14, 1, CAMERA_SENSOR_I2C_TYPE_BYTE) < 0)
		ctrl->meizu_cali_result = -5;
	for (i = 0; i < 3; i++) {
		msleep(300);
		if (meizu_ois_read(ctrl, 0x14, &value,
			CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0) {
			ctrl->meizu_cali_result = -6;
			goto out;
		}
		if (!value)
			break;
	}
	if (meizu_ois_read(ctrl, 4, &value, CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0) {
		ctrl->meizu_cali_result = -7;
		goto out;
	}
	/* Nonzero gyro status is logged by stock and does not stop flash save. */
	if (meizu_ois_write(ctrl, 3, 1, CAMERA_SENSOR_I2C_TYPE_BYTE) < 0)
		ctrl->meizu_cali_result = -8;
	for (i = 0; i < 3; i++) {
		msleep(300);
		if (meizu_ois_read(ctrl, 3, &value,
			CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0) {
			ctrl->meizu_cali_result = -9;
			goto out;
		}
		if (!value)
			break;
	}
	if (meizu_ois_read(ctrl, 0xfc, &value,
		CAMERA_SENSOR_I2C_TYPE_DWORD, true) < 0) {
		ctrl->meizu_cali_result = -10;
		goto out;
	}
	persisted = value != 0;
	rc = meizu_bsp_partition_write(&persisted, 1, 44, 0);
	if (rc)
		ctrl->meizu_cali_result = -12;
	rc = meizu_ois_read(ctrl, 0x63a, &value,
		CAMERA_SENSOR_I2C_TYPE_WORD, true);
	if (rc >= 0 && value == 2560) {
		persisted = 1;
		rc = meizu_bsp_partition_write(&persisted, 1, 45, 0);
		if (rc)
			CAM_ERR(CAM_OIS, "OIS temperature flag write failed %d", rc);
	}
	/* Stock final assignment clears transient write/temperature errors. */
	ctrl->meizu_cali_result = 0;
out:
	mutex_unlock(&ctrl->ois_mutex);
	return count;
}

/* The shipped eeprom_result is .data -1; exhaustive relocation references
 * are only the two loads in ois_cali_data_show. The shipped store always
 * returns -EINVAL after parsing its argument. Preserve this actual ABI.
 */
static ssize_t cali_data_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "-1\n");
}

static ssize_t cali_data_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return -EINVAL;
}

/* Stock S4H upgrade sends 112 bursts of 256 BYTEs to WORD address 0x100,
 * then the little-endian 16-bit word checksum plus 00 80 to address 8.
 */
static ssize_t fw_upgrade_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct cam_ois_ctrl_t *ctrl = dev_get_drvdata(dev);
	struct cam_sensor_i2c_reg_array *regs;
	struct cam_sensor_i2c_reg_setting setting = {
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	};
	u32 value = 0, checksum = 0;
	u8 persisted = 1;
	int request = 0, block, i, rc;
	ssize_t result = count;

	if (sscanf(buf, "%d", &request) != 1 || request != 1)
		return -EINVAL;
	regs = vzalloc(256 * sizeof(*regs));
	if (!regs)
		return -ENOMEM;
	setting.reg_setting = regs;
	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state != CAM_OIS_START) {
		ctrl->meizu_cali_result = -1;
		result = -EIO;
		goto out;
	}
	if (meizu_ois_write(ctrl, 0, 0, CAMERA_SENSOR_I2C_TYPE_BYTE) < 0)
		ctrl->meizu_cali_result = -2;
	msleep(100);
	if (meizu_ois_read(ctrl, 1, &value, CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0) {
		ctrl->meizu_cali_result = -3;
		goto out;
	}
	if (value != 1) {
		ctrl->meizu_cali_result = -4;
		goto out;
	}
	if (meizu_ois_write(ctrl, 0x0c, 0x75, CAMERA_SENSOR_I2C_TYPE_BYTE) < 0) {
		ctrl->meizu_cali_result = -5;
		goto out;
	}
	msleep(100);
	setting.size = 256;
	for (block = 0; block < 112; block++) {
		for (i = 0; i < 256; i++) {
			regs[i].reg_addr = 0x100;
			regs[i].reg_data = meizu_ois_s4h_firmware[block * 256 + i];
			usleep_range(1000, 1000);
		}
		rc = camera_io_dev_write_continuous(&ctrl->io_master_info, &setting, 0);
		if (rc < 0) {
			ctrl->meizu_cali_result = -6;
			goto out;
		}
		msleep(30);
	}
	if (meizu_ois_read(ctrl, 6, &value, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0) {
		ctrl->meizu_cali_result = -7;
		goto out;
	}
	/* Original nonzero pre-checksum status returns count without changing result. */
	if (value)
		goto out;
	for (i = 0; i < ARRAY_SIZE(meizu_ois_s4h_firmware); i += 2)
		checksum += get_unaligned_le16(&meizu_ois_s4h_firmware[i]);
	memset(regs, 0, 4 * sizeof(*regs));
	for (i = 0; i < 4; i++)
		regs[i].reg_addr = 8;
	regs[0].reg_data = checksum & 0xff;
	regs[1].reg_data = (checksum >> 8) & 0xff;
	regs[3].reg_data = 0x80;
	setting.size = 4;
	rc = camera_io_dev_write_continuous(&ctrl->io_master_info, &setting, 0);
	if (rc < 0) {
		ctrl->meizu_cali_result = -8;
		goto out;
	}
	msleep(300);
	if (meizu_ois_read(ctrl, 6, &value, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0) {
		ctrl->meizu_cali_result = -9;
		goto out;
	}
	if (!value) {
		if (meizu_ois_read(ctrl, 0xfc, &value,
			CAMERA_SENSOR_I2C_TYPE_DWORD, true) < 0) {
			ctrl->meizu_cali_result = -10;
			goto out;
		}
		if (value != 0xfa690000) {
			ctrl->meizu_cali_result = -11;
			goto out;
		}
	}
	/* Stock persists flag 1 even when the final register-6 status is nonzero. */
	rc = meizu_bsp_partition_write(&persisted, 1, 46, 0);
	ctrl->meizu_cali_result = rc ? -12 : 0;
out:
	mutex_unlock(&ctrl->ois_mutex);
	vfree(regs);
	return result;
}

static DEVICE_ATTR(fw_upgrade, 0664, ois_reset_show, fw_upgrade_store);
static DEVICE_ATTR(cali_data, 0664, cali_data_show, cali_data_store);
static DEVICE_ATTR(cali, 0664, cali_show, cali_store);
static DEVICE_ATTR(sanity, 0664, sanity_show, sanity_store);
static DEVICE_ATTR(ois_angle, 0664, ois_angle_show, ois_angle_store);
static DEVICE_ATTR(ois_reset, 0664, ois_reset_show, ois_reset_store);
static DEVICE_ATTR(ois_enable, 0664, ois_enable_show, ois_enable_store);
static DEVICE_ATTR(reg_data, 0664, reg_data_show, reg_data_store);
static DEVICE_ATTR(ois_xy, 0664, ois_xy_show, ois_xy_store);
static struct attribute *meizu_ois_attrs[] = {
	&dev_attr_fw_upgrade.attr, &dev_attr_cali_data.attr, &dev_attr_cali.attr, &dev_attr_sanity.attr, &dev_attr_ois_angle.attr, &dev_attr_ois_reset.attr, &dev_attr_ois_enable.attr, &dev_attr_reg_data.attr, &dev_attr_ois_xy.attr, NULL,
};
static const struct attribute_group meizu_ois_group = { .attrs = meizu_ois_attrs };

int cam_ois_meizu_sysfs_init(struct cam_ois_ctrl_t *ctrl)
{
	struct device_node *root;
	const char *version;
	bool matches = false;
	int rc;

	if (ctrl->soc_info.index != 0)
		return 0;
	root = of_find_node_by_path("/");
	if (root && !of_property_read_string(root, "meizu,chip-version", &version))
		matches = !strncmp(version, "M2391", 5);
	of_node_put(root);
	if (!matches)
		return 0;
	ctrl->meizu_enable_status = -1;
	ctrl->meizu_cali_result = -1;
	ctrl->meizu_sanity_criterion = 60;
	ctrl->meizu_sanity_count = 100;
	ctrl->meizu_sanity_level = 10;
	rc = sysfs_create_group(&ctrl->soc_info.dev->kobj, &meizu_ois_group);
	if (rc)
		return rc;
	rc = meizu_sysfslink_register_name(ctrl->soc_info.dev, "ois");
	if (rc) {
		sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_ois_group);
		return rc;
	}
	mutex_lock(&meizu_ois_registry_lock);
	meizu_primary_ois = ctrl;
	mutex_unlock(&meizu_ois_registry_lock);
	ctrl->meizu_sysfs_ready = true;
	return 0;
}

void cam_ois_meizu_sysfs_remove(struct cam_ois_ctrl_t *ctrl)
{
	if (ctrl->meizu_sysfs_ready) {
		meizu_sysfslink_remove_name("ois");
		sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_ois_group);
		mutex_lock(&meizu_ois_registry_lock);
		if (meizu_primary_ois == ctrl) meizu_primary_ois = NULL;
		mutex_unlock(&meizu_ois_registry_lock);
		ctrl->meizu_sysfs_ready = false;
	}
}

/* Original no-argument exported register self-test. Registry lifetime is held
 * through the hardware sequence, so unbind drains this API before ctrl free.
 */
int mz_ois_self_test(void)
{
	struct cam_ois_ctrl_t *ctrl;
	u32 enabled, mode, x, y;
	int rc = -5;

	mutex_lock(&meizu_ois_registry_lock);
	ctrl = meizu_primary_ois;
	if (!ctrl) goto out;
	mutex_lock(&ctrl->ois_mutex);
	if (ctrl->cam_ois_state <= CAM_OIS_ACQUIRE) { rc = -EINVAL; goto unlock; }
	rc = -1;
	if (meizu_ois_write(ctrl, 0x80, 1, CAMERA_SENSOR_I2C_TYPE_BYTE) < 0 ||
		meizu_ois_read(ctrl, 0, &enabled, CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0)
		goto unlock;
	if (enabled != 1) { rc = -2; goto unlock; }
	if (meizu_ois_read(ctrl, 2, &mode, CAMERA_SENSOR_I2C_TYPE_BYTE, true) < 0)
		goto unlock;
	if (mode != 16) { rc = -3; goto unlock; }
	if (meizu_ois_read(ctrl, 0x82, &x, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0 ||
		meizu_ois_read(ctrl, 0x84, &y, CAMERA_SENSOR_I2C_TYPE_WORD, true) < 0)
		goto unlock;
	mdelay(5);
	rc = x && y ? 0 : -4;
unlock:
	mutex_unlock(&ctrl->ois_mutex);
out:
	mutex_unlock(&meizu_ois_registry_lock);
	return rc;
}
EXPORT_SYMBOL(mz_ois_self_test);
