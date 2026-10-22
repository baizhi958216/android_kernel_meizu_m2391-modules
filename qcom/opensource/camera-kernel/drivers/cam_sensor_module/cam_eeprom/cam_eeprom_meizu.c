// SPDX-License-Identifier: GPL-2.0-only
/*
 * M2391 EEPROM readers reconstructed from camera.ko .text 0x19fb14..0x1a0b40.
 * Identity/checksum data comes from ELF records, not another phone's driver.
 * Readers, checksum exemptions and the proven 131-byte force-flag transaction
 * and bounded TOF patch transactions are exposed; automatic EEPROM-to-partition
 * synchronization remains separate from these recovered sysfs operations.
 */
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <asm/unaligned.h>
#include "cam_eeprom_meizu.h"
#include "cam_eeprom_meizu_golden.h"
#include "cam_debug_util.h"
#include "../../../../../../meizu/system/meizu_sys.h"
#include "../../../../../../meizu/storage/meizu_storage.h"

static u32 meizu_checksum_exempt[5] = { 0, 0, 0x3000, 0x3000, 0 };
static int meizu_otp_request[4] = { 0, 47, 0, 47 };
static u8 meizu_update_flag;
static DEFINE_MUTEX(meizu_eeprom_control_lock);
static DEFINE_MUTEX(meizu_partition_lock);
static const char * const meizu_eeprom_links[] = {
	"rcam_otp", "fcam_otp", "rcam2_otp", "rcam3_otp", "tof_otp",
};

static bool meizu_m2391_machine(void)
{
	struct device_node *root = of_find_node_by_path("/");
	const char *version;
	bool matches = false;

	if (root && !of_property_read_string(root, "meizu,chip-version", &version))
		matches = !strncmp(version, "M2391", 5);
	of_node_put(root);
	return matches;
}

/* The generic driver frees mapdata after a request; retain a private snapshot
 * for the stock diagnostic ABI instead of leaving its sysfs readers dangling.
 * Called with eeprom_mutex held after a successful complete memory read.
 */
void cam_eeprom_meizu_cache_otp(struct cam_eeprom_ctrl_t *ctrl)
{
	u8 *copy;

	if (!meizu_m2391_machine() ||
		ctrl->eeprom_device_type != MSM_CAMERA_PLATFORM_DEVICE ||
		ctrl->soc_info.index >= 5 ||
		!ctrl->cal_data.mapdata ||
		!ctrl->cal_data.num_data)
		return;
	copy = kmemdup(ctrl->cal_data.mapdata, ctrl->cal_data.num_data, GFP_KERNEL);
	if (!copy)
		return;
	kfree(ctrl->meizu_otp_data);
	ctrl->meizu_otp_data = copy;
	ctrl->meizu_otp_size = ctrl->cal_data.num_data;
}

/* Stock set_data_to_partition/get_camera_golden_hex: model bytes '8' and
 * '9' choose four exact embedded auxiliary-camera calibration tables.
 * The first four table bytes encode BE16 destination start/end; data contains
 * one flag, 2048 sample bytes and a BE16 sum%65535+1 checksum.
 */
int cam_eeprom_meizu_apply_golden(u8 *bytes, u32 length, u32 index)
{
	const u8 *golden = NULL;
	u32 start, end, sum = 0, i;

	if (!bytes || length <= 5)
		return -EINVAL;
	if (bytes[5] == '8') {
		if (index == 2)
			golden = golden_hex_5e9;
		else if (index == 3)
			golden = golden_hex_3p9;
	} else if (bytes[5] == '9') {
		if (index == 2)
			golden = golden_hex_jn105;
		else if (index == 3)
			golden = golden_hex_jn103;
	} else {
		return -1;
	}
	if (!golden)
		return 0;
	start = get_unaligned_be16(golden);
	end = get_unaligned_be16(golden + 2);
	if (end != start + 2050 || start > length || 2051 > length - start)
		return -EINVAL;
	for (i = 5; i < 2053; i++)
		sum += golden[i];
	memcpy(bytes + start, golden + 4, 2051);
	put_unaligned_be16(sum % 65535 + 1, bytes + start + 2049);
	return 0;
}

static int meizu_eeprom_page_write(struct cam_eeprom_ctrl_t *ctrl,
		const struct cam_eeprom_map_t *page, bool disable)
{
	struct cam_sensor_i2c_reg_array reg = {
		.reg_addr = page->addr, .reg_data = disable ? 0 : page->data,
		.delay = page->delay,
	};
	struct cam_sensor_i2c_reg_setting setting = {
		.reg_setting = &reg, .size = 1,
		.addr_type = page->addr_type, .data_type = page->data_type,
	};

	return camera_io_dev_write(&ctrl->io_master_info, &setting);
}

/* Complete M2391 map read policy recovered from is_need_reload_eeprom,
 * set_data_to_partition and cam_eeprom_read_memory. A return of one means
 * this map has been consumed; zero leaves other machines to the generic path.
 * Caller holds eeprom_mutex and has applied this map's slave address.
 */
int cam_eeprom_meizu_read_map(struct cam_eeprom_ctrl_t *ctrl,
		const struct cam_eeprom_memory_map_t *map, u8 *destination)
{
	u8 metadata[561], identity[40], other_metadata[561];
	u32 index, size, golden_flag = 0, counter;
	bool metadata_changed, serial_changed = false, reload, page_enabled = false;
	int rc;

	if (!meizu_m2391_machine() ||
	    ctrl->eeprom_device_type != MSM_CAMERA_PLATFORM_DEVICE ||
	    ctrl->soc_info.index >= 5 || !map->mem.valid_size)
		return 0;
	index = ctrl->soc_info.index;
	size = map->mem.valid_size;
	if (size < sizeof(identity) || size > 25600)
		return -EINVAL;
	mutex_lock(&meizu_partition_lock);
	rc = sd_partition_rw("camera", 0, (loff_t)index * 25600,
		metadata, sizeof(metadata));
	if (rc < 0)
		goto out;
	rc = camera_io_dev_read_seq(&ctrl->io_master_info, map->mem.addr,
		identity, map->mem.addr_type, map->mem.data_type, sizeof(identity));
	if (rc < 0)
		goto out;
	metadata_changed = metadata[128] == 1 || metadata[1] != identity[1] ||
		get_unaligned_le16(metadata + 129) != size;
	if (!metadata_changed)
		serial_changed = memcmp(metadata + 29, identity + 29, 11) != 0;
	reload = metadata_changed || serial_changed;
	if (reload) {
		memcpy(metadata, identity, sizeof(identity));
		metadata[128] = 0;
		put_unaligned_le16(size, metadata + 129);
		if (serial_changed) {
			counter = (u32)metadata[501] + 1;
			metadata[501] = counter > 255 ? 1 : counter;
		}
	} else {
		rc = sd_partition_rw("camera", 0, (loff_t)(index + 10) * 25600,
			destination, size);
		if (rc < 0)
			goto out;
		if (metadata[5] == '8') {
			if (index == 2)
				golden_flag = 1943;
			else if (index == 3)
				golden_flag = 3142;
		} else if (metadata[5] == '9' && (index == 2 || index == 3)) {
			golden_flag = 12996;
		}
		if (golden_flag) {
			if (golden_flag >= size) { rc = -EINVAL; goto out; }
			/* Defer flag persistence to successful full physical refresh.
			 * Stock wrote it first and could accept stale data next probe.
			 */
			if (!destination[golden_flag])
				reload = true;
		}
		if (!reload) { rc = 1; goto out; }
	}
	if (map->page.valid_size) {
		rc = meizu_eeprom_page_write(ctrl, &map->page, false);
		if (rc)
			goto out;
	}
	if (map->pageen.valid_size) {
		page_enabled = true;
		rc = meizu_eeprom_page_write(ctrl, &map->pageen, false);
		if (rc)
			goto out;
	}
	if (map->poll.valid_size) {
		rc = camera_io_dev_poll(&ctrl->io_master_info, map->poll.addr,
			map->poll.data, 0, map->poll.addr_type,
			map->poll.data_type, map->poll.delay);
		if (rc)
			goto out;
	}
	rc = camera_io_dev_read_seq(&ctrl->io_master_info, map->mem.addr,
		destination, map->mem.addr_type, map->mem.data_type, size);
	if (rc < 0)
		goto out;
	rc = cam_eeprom_meizu_apply_golden(destination, size, index);
	if (rc)
		goto out;
	rc = sd_partition_rw("camera", 1, (loff_t)(index + 10) * 25600,
		destination, size);
	if (rc < 0)
		goto out;
	/* Commit identity only after successful physical read and payload write;
	 * stock committed first and could mark failed reads as fresh.
	 */
	if (metadata_changed || serial_changed) {
		rc = sd_partition_rw("camera", 1, (loff_t)index * 25600,
			metadata, sizeof(metadata));
		if (rc < 0)
			goto out;
	}
	if (serial_changed && index == 0) {
		rc = sd_partition_rw("camera", 0, (loff_t)3 * 25600,
			other_metadata, sizeof(other_metadata));
		if (rc < 0)
			goto out;
		other_metadata[128] = 1;
		rc = sd_partition_rw("camera", 1, (loff_t)3 * 25600,
			other_metadata, sizeof(other_metadata));
		if (rc < 0)
			goto out;
	}
	if (map->pageen.valid_size) {
		rc = meizu_eeprom_page_write(ctrl, &map->pageen, true);
		page_enabled = false;
		if (rc)
			goto out;
	}
	rc = 1;
out:
	if (page_enabled)
		meizu_eeprom_page_write(ctrl, &map->pageen, true);
	mutex_unlock(&meizu_partition_lock);
	return rc;
}

static const char *meizu_camera_lookup(const struct meizu_camera_name *table,
		size_t count, u8 id)
{
	size_t i;

	for (i = 0; i < count; i++)
		if ((u8)table[i].id == id)
			return table[i].name;
	return NULL;
}

/* Stock stores for these four files simply return count, without writes. */
static ssize_t meizu_diagnostic_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t actuator_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	const char *name;
	ssize_t rc;

	if (!ctrl)
		return sysfs_emit(buf, "null\n");
	mutex_lock(&ctrl->eeprom_mutex);
	if (!ctrl->meizu_otp_data) {
		rc = sysfs_emit(buf, "null\n");
	} else {
		name = meizu_camera_lookup(meizu_camera_actuator_list, 5,
			ctrl->soc_info.index);
		rc = sysfs_emit(buf, "%s\n", name ? name : "");
	}
	mutex_unlock(&ctrl->eeprom_mutex);
	return rc;
}

static ssize_t focus_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	int infinity = -1, macro = 0;

	if (ctrl) {
		mutex_lock(&ctrl->eeprom_mutex);
		if (ctrl->meizu_otp_data && ctrl->meizu_otp_size >= 159) {
			infinity = get_unaligned_be16(ctrl->meizu_otp_data + 153);
			macro = get_unaligned_be16(ctrl->meizu_otp_data + 157);
		}
		mutex_unlock(&ctrl->eeprom_mutex);
	}
	return sysfs_emit(buf, "AF_Infinity=%d\nAF_Macro=%d\n", infinity, macro);
}

static ssize_t module_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	const char *module, *sensor;
	u8 *data;
	int rc, length = 0, i;
	u32 index;

	if (!ctrl)
		return -EINVAL;
	index = ctrl->soc_info.index;
	if (index >= 55)
		return -ERANGE;
	data = kzalloc(512, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = sd_partition_rw("camera", 0, (loff_t)(index + 10) * 25600, data, 512);
	if (rc < 0)
		goto out;
	sensor = meizu_camera_lookup(meizu_camera_sensor_list, 32, data[14]);
	module = meizu_camera_lookup(meizu_camera_module_list, 18, data[1]);
	if (!module) {
		/* Original returns -1 after formatting this diagnostic. */
		sysfs_emit(buf, "Unkown Module - %d\n", index);
		rc = -1;
		goto out;
	}
	length = sysfs_emit(buf, "%s-%s\nSN: 0x", sensor ? sensor : "", module);
	for (i = 29; i <= 39; i++)
		length += sysfs_emit_at(buf, length, "%02x", data[i]);
	length += sysfs_emit_at(buf, length, "\n");
	if (index == 0)
		length += sysfs_emit_at(buf, length,
			"ois_gyro_offset_cali flag:   %d\n"
			"ois_temperature_offset flag: %d\n"
			"ois_fw_upgrade flag:        %d\n", data[44], data[45], data[46]);
	rc = length;
out:
	kfree(data);
	return rc;
}

static ssize_t ois_gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 *data;
	u32 gain_x = 0, gain_y = 0;
	u16 db_x = 0, db_y = 0;
	int rc;

	data = kzalloc(3170, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	/* Stock fixed main-camera slot: 0x3e800 == 10 * 25600. */
	rc = sd_partition_rw("camera", 0, 256000, data, 3170);
	if (rc < 0)
		goto out;
	if (data[3157] == 1) {
		gain_x = get_unaligned_be32(data + 3158);
		gain_y = get_unaligned_be32(data + 3162);
		db_x = get_unaligned_be16(data + 3166);
		db_y = get_unaligned_be16(data + 3168);
	}
	rc = sysfs_emit(buf, "ois_gain: x:0x%08x y:0x%08x\n"
		"x_perfomanc:%d(db/100) y_perfomanc:%d(db/100)\n",
		gain_x, gain_y, db_x, db_y);
out:
	kfree(data);
	return rc;
}

/* Stock mz_check_otp_data, including per-camera TOF checksum byte order,
 * 65535 modulus, skipped ranges and the original integer error encoding.
 * Unlike stock, reject truncated partition data before indexing any byte.
 */
static int meizu_check_otp(u32 index, const u8 *data, size_t size,
		const struct meizu_otp_range *table, size_t count)
{
	size_t i;
	u32 j, sum, stored, high, low;
	const struct meizu_otp_range *range;

	for (i = 0; i < count; i++) {
		range = &table[i];
		if (range->flag == 0xffff)
			continue;
		if (READ_ONCE(meizu_checksum_exempt[index]) & BIT(i))
			continue;
		if (range->flag >= size || range->end >= size ||
			range->high >= size || range->low >= size)
			return -EINVAL;
		if (data[range->flag] != 1)
			return -((index << 12) | i);
		sum = 0;
		for (j = range->start; j <= range->end; j++)
			sum += data[j];
		sum = sum % 65535 + 1;
		high = index == 4 ? range->low : range->high;
		low = index == 4 ? range->high : range->low;
		stored = (data[high] << 8) | data[low];
		if (sum != stored)
			return (index << 12) | i | 0x100;
	}
	return 1;
}

static ssize_t cam_cal_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	const struct meizu_otp_range *tables[] = { mz_rear_e2p_sum, mz_face_e2p_sum,
		mz_admi_e2p_sum, mz_admi2_e2p_sum, mz_tof_e2p_sum };
	const size_t counts[] = { 6, 3, 14, 14, 11 };
	u8 header[131] = { 0 }, *data;
	u32 index, size;
	int rc;

	if (!ctrl)
		return -EINVAL;
	index = ctrl->soc_info.index;
	if (index >= ARRAY_SIZE(tables))
		return -EINVAL;
	mutex_lock(&ctrl->eeprom_mutex);
	if (!ctrl->meizu_otp_data) {
		mutex_unlock(&ctrl->eeprom_mutex);
		return sysfs_emit(buf, "0\n");
	}
	mutex_unlock(&ctrl->eeprom_mutex);
	rc = sd_partition_rw("camera", 0, (loff_t)index * 25600, header, sizeof(header));
	if (rc < 0)
		return rc;
	size = get_unaligned_le16(header + 129);
	if (!size)
		return sysfs_emit(buf, "0\n");
	if (size > 25600)
		return -ERANGE;
	data = kzalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = sd_partition_rw("camera", 0, (loff_t)(index + 10) * 25600, data, size);
	if (rc >= 0)
		rc = meizu_check_otp(index, data, size, tables[index], counts[index]);
	kfree(data);
	return sysfs_emit(buf, "%d\n", rc);
}

static ssize_t cam_cal_check_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u32 index = 0, exempt = 0;

	if (sscanf(buf, "%u %x", &index, &exempt) != 2)
		return -EINVAL;
	if (index < ARRAY_SIZE(meizu_checksum_exempt))
		WRITE_ONCE(meizu_checksum_exempt[index], exempt);
	return count;
}

/* The force flag is one-way here: stock writes only when flag == 1. */
static int meizu_reset_partition_force_flag(u32 index, u8 flag)
{
	u8 header[131] = { 0 };
	loff_t offset = (loff_t)index * 25600;
	int rc;

	if (index >= 65)
		return -EINVAL;
	rc = sd_partition_rw("camera", 0, offset, header, sizeof(header));
	if (rc < 0)
		return rc;
	if (flag == 1) {
		header[128] = 1;
		rc = sd_partition_rw("camera", 1, offset, header, sizeof(header));
		if (rc < 0)
			return rc;
		msleep(5);
		rc = sd_partition_rw("camera", 0, offset, header, sizeof(header));
		if (rc < 0)
			return rc;
	}
	return header[128];
}

static ssize_t update_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	u8 header[131] = { 0 };
	int rc;

	if (!ctrl)
		return sysfs_emit(buf, "null\n");
	mutex_lock(&ctrl->eeprom_mutex);
	if (!ctrl->meizu_otp_data) {
		rc = sysfs_emit(buf, "null\n");
		goto out;
	}
	rc = sd_partition_rw("camera", 0,
		(loff_t)ctrl->soc_info.index * 25600, header, sizeof(header));
	if (rc >= 0) {
		mutex_lock(&meizu_eeprom_control_lock);
		meizu_update_flag = header[128];
		rc = sysfs_emit(buf, "%d.\n", meizu_update_flag);
		mutex_unlock(&meizu_eeprom_control_lock);
	}
out:
	mutex_unlock(&ctrl->eeprom_mutex);
	return rc;
}

static ssize_t update_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	int flag;

	if (sscanf(buf, "%d", &flag) != 1)
		return -EINVAL;
	if (!ctrl)
		return count;
	mutex_lock(&ctrl->eeprom_mutex);
	if (ctrl->meizu_otp_data) {
		mutex_lock(&meizu_eeprom_control_lock);
		meizu_update_flag = (u8)flag;
		mutex_lock(&meizu_partition_lock);
		meizu_reset_partition_force_flag(ctrl->soc_info.index, meizu_update_flag);
		mutex_unlock(&meizu_partition_lock);
		mutex_unlock(&meizu_eeprom_control_lock);
	}
	mutex_unlock(&ctrl->eeprom_mutex);
	return count;
}

static int meizu_dump_partition(int index, int size, int start, int end)
{
	u8 header[187] = { 0 }, *data;
	int rc, i;
	size_t length;

	/* Stock debug controls lack bounds checks; do not reproduce its OOB
	 * read/heap overwrite when size < 187 or end == size (the default).
	 */
	if (index < 0 || index >= 55 || size <= 0 || size > 25600 ||
		start < 0 || end < start || end > size)
		return -EINVAL;
	rc = sd_partition_rw("camera", 0, (loff_t)index * 25600,
		header, sizeof(header));
	if (rc < 0)
		return rc;
	length = min_t(size_t, get_unaligned_le16(header + 129), size);
	data = kzalloc(size + 1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = sd_partition_rw("camera", 0, (loff_t)(index + 10) * 25600,
		data, length);
	if (rc >= 0)
		for (i = start; i <= end; i++)
			CAM_INFO(CAM_EEPROM, "REG[%04X] DATA[%02X]", i, data[i]);
	kfree(data);
	return rc;
}

static ssize_t cam_otp_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	int rc;

	if (!ctrl)
		return sysfs_emit(buf, "null\n");
	mutex_lock(&ctrl->eeprom_mutex);
	if (!ctrl->meizu_otp_data) {
		rc = sysfs_emit(buf, "null\n");
	} else {
		mutex_lock(&meizu_eeprom_control_lock);
		meizu_dump_partition(meizu_otp_request[0], meizu_otp_request[1],
			meizu_otp_request[2], meizu_otp_request[3]);
		rc = sysfs_emit(buf, "%d.\n", meizu_otp_request[0]);
		mutex_unlock(&meizu_eeprom_control_lock);
	}
	mutex_unlock(&ctrl->eeprom_mutex);
	return rc;
}

static ssize_t cam_otp_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int request[4];

	if (sscanf(buf, "%d %d %d %d", &request[0], &request[1],
		&request[2], &request[3]) != 4)
		return -EINVAL;
	mutex_lock(&meizu_eeprom_control_lock);
	memcpy(meizu_otp_request, request, sizeof(request));
	mutex_unlock(&meizu_eeprom_control_lock);
	return count;
}

static ssize_t tof_data_cali_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	static const u32 offsets[] = { 347, 348, 349, 350, 5159, 5160,
		5229, 5230, 5231, 5232, 10041, 10042 };
	static const char * const labels[] = {
		"Mode1_offset1:    ", "Mode1_offset2:    ", "Mode1_offset3:    ",
		"Mode1_offset4:    ", "Mode1_check_sum1: ", "Mode1_check_sum2: ",
		"Mode2_offset1:    ", "Mode3_offset2:    ", "Mode2_offset3:    ",
		"Mode2_offset4:    ", "Mode2_check_sum1: ", "Mode2_check_sum2: ",
	};
	u8 *data;
	int rc, length = 0, i;

	if (!ctrl || ctrl->soc_info.index >= 55)
		return -EINVAL;
	data = kzalloc(10043, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = sd_partition_rw("camera", 0,
		(loff_t)(ctrl->soc_info.index + 10) * 25600, data, 10043);
	if (rc >= 0) {
		for (i = 0; i < ARRAY_SIZE(offsets); i++)
			length += sysfs_emit_at(buf, length, "%s0x%x.\n",
				labels[i], data[offsets[i]]);
		rc = length;
	}
	kfree(data);
	return rc;
}

/* Stock meizu_tofcali_write reads the declared calibration block, patches
 * bytes and writes that same block back. Reject failed/truncated reads and
 * offset overruns before allowing the recovered write-back operation.
 */
static int meizu_partition_patch_locked(const u8 *bytes, u32 count, u32 offset, u8 index)
{
	u8 header[131] = { 0 }, *data;
	u32 length;
	int rc;

	if (index >= 5 || (!bytes && count))
		return -EINVAL;
	rc = sd_partition_rw("camera", 0, (loff_t)index * 25600,
		header, sizeof(header));
	if (rc < 0)
		return rc;
	length = get_unaligned_le16(header + 129);
	if (!length || length > 25600 || offset > length || count > length - offset)
		return -EINVAL;
	data = kzalloc(length, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = sd_partition_rw("camera", 0, (loff_t)(index + 10) * 25600,
		data, length);
	if (rc >= 0) {
		memcpy(data + offset, bytes, count);
		rc = sd_partition_rw("camera", 1, (loff_t)(index + 10) * 25600,
			data, length);
	}
	kfree(data);
	return rc;
}

int meizu_bsp_partition_write(const u8 *bytes, u32 count, u32 offset, u8 index)
{
	int rc;

	mutex_lock(&meizu_partition_lock);
	rc = meizu_partition_patch_locked(bytes, count, offset, index);
	mutex_unlock(&meizu_partition_lock);
	return rc;
}

/* Stock cam_eeprom_driver_cmd opcode 0x202 receives exactly 16 bytes:
 * u32 payload offset, u32 byte count, u64 user pointer. Payload read-modify-
 * write is followed by preserving the 187-byte metadata and setting byte131.
 * Caller holds eeprom_mutex; the shared partition lock also covers OIS writes.
 */
int cam_eeprom_meizu_write_partition(struct cam_eeprom_ctrl_t *ctrl, u64 handle)
{
	struct { u32 offset, count; u64 buffer; } request;
	u8 header[187], *bytes;
	u32 index;
	int rc;

	if (!ctrl || !meizu_m2391_machine() ||
	    ctrl->eeprom_device_type != MSM_CAMERA_PLATFORM_DEVICE)
		return -ENODEV;
	index = ctrl->soc_info.index;
	if (index >= 5)
		return -EINVAL;
	if (copy_from_user(&request, u64_to_user_ptr(handle), sizeof(request)))
		return -EFAULT;
	if (!request.count || request.count > 25600 || request.offset > 25600 ||
	    request.count > 25600 - request.offset)
		return -EINVAL;
	bytes = memdup_user(u64_to_user_ptr(request.buffer), request.count);
	if (IS_ERR(bytes))
		return PTR_ERR(bytes);
	mutex_lock(&meizu_partition_lock);
	rc = sd_partition_rw("camera", 0, (loff_t)index * 25600,
		header, sizeof(header));
	if (rc < 0)
		goto out;
	rc = meizu_partition_patch_locked(bytes, request.count, request.offset, index);
	if (rc < 0)
		goto out;
	header[131] = 1;
	rc = sd_partition_rw("camera", 1, (loff_t)index * 25600,
		header, sizeof(header));
out:
	mutex_unlock(&meizu_partition_lock);
	kfree(bytes);
	return rc;
}

/* Stock meizu_bsp_eeprom_write uses WORD register addresses, BYTE values,
 * one transaction per byte, and a 5000us delay even after a failed write.
 * Keep the original last-transfer return contract. Device selection, power
 * and serialization are explicit caller responsibilities in this source API.
 */
int cam_eeprom_meizu_write_bytes(struct cam_eeprom_ctrl_t *ctrl,
		const u8 *bytes, u32 count, u32 address)
{
	struct cam_sensor_i2c_reg_array reg = { 0 };
	struct cam_sensor_i2c_reg_setting setting = {
		.reg_setting = &reg,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	};
	u32 i;
	int rc = 0;

	if (!ctrl || (!bytes && count) || address > 0xffff || count > 0x10000 - address)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		reg.reg_addr = address + i;
		reg.reg_data = bytes[i];
		rc = camera_io_dev_write(&ctrl->io_master_info, &setting);
		if (rc < 0)
			CAM_ERR(CAM_EEPROM, "Meizu EEPROM byte write failed: %d", rc);
		usleep_range(5000, 5000);
	}
	return rc;
}

static ssize_t tof_data_cali_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	u32 offset = 0, size = 0, value = 0;
	u8 bytes[5];
	int rc;

	if (!ctrl)
		return -EINVAL;
	if (sscanf(buf, "0x%x %u 0x%x", &offset, &size, &value) != 3)
		return -EINVAL;
	/* Stock permits at most five bytes from its zero-initialized u32 buffer. */
	if (size > sizeof(bytes))
		return -EINVAL;
	put_unaligned_le32(value, bytes);
	bytes[4] = 0;
	mutex_lock(&ctrl->eeprom_mutex);
	if (!ctrl->meizu_otp_data) {
		mutex_unlock(&ctrl->eeprom_mutex);
		return -EINVAL;
	}
	rc = meizu_bsp_partition_write(bytes, size, offset, ctrl->soc_info.index);
	if (rc < 0)
		CAM_ERR(CAM_EEPROM, "TOF calibration write failed: %d", rc);
	mutex_unlock(&ctrl->eeprom_mutex);
	return count;
}

static ssize_t tof_pd_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(dev);
	u8 data[55] = { 0 };
	int rc;

	if (!ctrl || ctrl->soc_info.index >= 55)
		return -EINVAL;
	/* Stock reads 54 bytes into a zeroed 55-byte buffer; final byte stays 0. */
	rc = sd_partition_rw("camera", 0,
		(loff_t)(ctrl->soc_info.index + 10) * 25600, data, 54);
	if (rc < 0)
		return rc;
	return sysfs_emit(buf, "PD_flag:   0x%x.\nPD1:       0x%x.\n"
		"PD2:       0x%x.\nCheckSum1: 0x%x.\nCheckSum1: 0x%x.\n",
		data[50], data[51], data[52], data[53], data[54]);
}

static DEVICE_ATTR(actuator_info, 0664, actuator_info_show, meizu_diagnostic_store);
static DEVICE_ATTR(focus_info, 0664, focus_info_show, meizu_diagnostic_store);
static DEVICE_ATTR(module_info, 0664, module_info_show, meizu_diagnostic_store);
static DEVICE_ATTR(ois_gain, 0664, ois_gain_show, meizu_diagnostic_store);
static DEVICE_ATTR(cam_cal_check, 0664, cam_cal_check_show, cam_cal_check_store);
static DEVICE_ATTR(update_enable, 0664, update_enable_show, update_enable_store);
static DEVICE_ATTR(cam_otp, 0664, cam_otp_show, cam_otp_store);
static DEVICE_ATTR(tof_data_cali, 0664, tof_data_cali_show, tof_data_cali_store);
static DEVICE_ATTR(tof_pd_data, 0664, tof_pd_data_show, meizu_diagnostic_store);
static struct attribute *meizu_eeprom_attrs[] = {
	&dev_attr_actuator_info.attr, &dev_attr_focus_info.attr,
	&dev_attr_module_info.attr, &dev_attr_ois_gain.attr,
	&dev_attr_cam_cal_check.attr, &dev_attr_update_enable.attr,
	&dev_attr_cam_otp.attr, &dev_attr_tof_data_cali.attr, &dev_attr_tof_pd_data.attr, NULL,
};

static umode_t meizu_eeprom_visible(struct kobject *kobj, struct attribute *attr,
		int unused)
{
	struct cam_eeprom_ctrl_t *ctrl = dev_get_drvdata(kobj_to_dev(kobj));

	if (!ctrl)
		return 0;
	if (ctrl->soc_info.index != 4 &&
		(attr == &dev_attr_tof_data_cali.attr || attr == &dev_attr_tof_pd_data.attr))
		return 0;
	if ((ctrl->soc_info.index == 1 || ctrl->soc_info.index == 4) &&
		(attr == &dev_attr_focus_info.attr || attr == &dev_attr_ois_gain.attr))
		return 0;
	return attr->mode;
}

static const struct attribute_group meizu_eeprom_group = {
	.attrs = meizu_eeprom_attrs,
	.is_visible = meizu_eeprom_visible,
};

int cam_eeprom_meizu_sysfs_init(struct cam_eeprom_ctrl_t *ctrl)
{
	int rc;

	if (!meizu_m2391_machine() || ctrl->soc_info.index >= ARRAY_SIZE(meizu_eeprom_links))
		return 0;
	rc = sysfs_create_group(&ctrl->soc_info.dev->kobj, &meizu_eeprom_group);
	if (rc)
		return rc;
	rc = meizu_sysfslink_register_name(ctrl->soc_info.dev,
		(char *)meizu_eeprom_links[ctrl->soc_info.index]);
	if (rc) {
		sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_eeprom_group);
		return rc;
	}
	ctrl->meizu_sysfs_ready = true;
	return 0;
}

void cam_eeprom_meizu_sysfs_remove(struct cam_eeprom_ctrl_t *ctrl)
{
	if (ctrl->meizu_sysfs_ready) {
		meizu_sysfslink_remove_name((char *)meizu_eeprom_links[ctrl->soc_info.index]);
		sysfs_remove_group(&ctrl->soc_info.dev->kobj, &meizu_eeprom_group);
		ctrl->meizu_sysfs_ready = false;
	}
	mutex_lock(&ctrl->eeprom_mutex);
	kfree(ctrl->meizu_otp_data);
	ctrl->meizu_otp_data = NULL;
	ctrl->meizu_otp_size = 0;
	mutex_unlock(&ctrl->eeprom_mutex);
}
