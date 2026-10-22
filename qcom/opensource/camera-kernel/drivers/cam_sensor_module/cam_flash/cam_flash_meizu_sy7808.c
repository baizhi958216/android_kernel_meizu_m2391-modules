// SPDX-License-Identifier: GPL-2.0-only
/* Actual SY7808 leaf operations recovered from M2391 camera.ko.
 * Provider probe/LED-trigger integration is deliberately not enabled until
 * its complete original callback and power lifetime are recovered.
 */
#include "cam_flash_meizu_sy7808.h"
#include "cam_debug_util.h"

static int meizu_sy7808_read(struct meizu_sy7808 *chip, u32 address, u32 *value)
{
	return camera_io_dev_read(chip->io, address, value,
		CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE, true);
}

static int meizu_sy7808_write(struct meizu_sy7808 *chip, u32 address, u32 value)
{
	struct cam_sensor_i2c_reg_array reg = {
		.reg_addr = address, .reg_data = (u8)value,
	};
	struct cam_sensor_i2c_reg_setting setting = {
		.reg_setting = &reg, .size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	};
	int rc = camera_io_dev_write(chip->io, &setting);

	if (rc < 0)
		CAM_ERR(CAM_FLASH, "SY7808 write 0x%x=0x%x failed %d", address, value, rc);
	return rc;
}

/* Stock init increments users before I/O; register write failures log and
 * continue through the complete initialization sequence.
 */
int meizu_sy7808_init(struct meizu_sy7808 *chip)
{
	static const u8 sequence[][2] = {
		{ 1, 0 }, { 10, 15 }, { 3, 0 }, { 4, 0 },
		{ 5, 0 }, { 6, 0 }, { 7, 0 }, { 8, 0 },
	};
	int i;

	if (!chip || !chip->io)
		return -EINVAL;
	mutex_lock(&chip->lock);
	if (!chip->users++)
		for (i = 0; i < ARRAY_SIZE(sequence); i++)
			meizu_sy7808_write(chip, sequence[i][0], sequence[i][1]);
	mutex_unlock(&chip->lock);
	return 0;
}

int meizu_sy7808_release(struct meizu_sy7808 *chip)
{
	if (!chip || !chip->io)
		return -EINVAL;
	mutex_lock(&chip->lock);
	if (chip->users && !--chip->users) {
		meizu_sy7808_write(chip, 5, 0);
		meizu_sy7808_write(chip, 1, 0);
		chip->mode = 0;
		chip->enabled_channels = 0;
	}
	mutex_unlock(&chip->lock);
	return 0;
}

/* Stock shutdown only sends these two writes; refcounts are unchanged. */
int meizu_sy7808_shutdown(struct meizu_sy7808 *chip)
{
	if (!chip || !chip->io)
		return -EINVAL;
	mutex_lock(&chip->lock);
	meizu_sy7808_write(chip, 5, 0);
	meizu_sy7808_write(chip, 1, 0);
	mutex_unlock(&chip->lock);
	return 0;
}

/* Stock 0x19cbdc: mode2 is torch, all other enable modes use flash bits. */
int meizu_sy7808_enable(struct meizu_sy7808 *chip, u32 mode, u32 channels)
{
	u32 value = 0;
	int rc;

	if (!chip || !chip->io)
		return -EINVAL;
	mutex_lock(&chip->lock);
	rc = meizu_sy7808_read(chip, 1, &value);
	if (rc < 0)
		goto out;
	value |= mode == 2 ? 8 : 12;
	if (channels & 3) {
		value |= channels & 3;
		rc = meizu_sy7808_write(chip, 1, value);
		if (rc < 0)
			goto out;
	}
	if (channels & 4) {
		rc = meizu_sy7808_write(chip, 1, value);
		if (rc < 0)
			goto out;
		rc = meizu_sy7808_read(chip, 5, &value);
		if (rc < 0)
			goto out;
		rc = meizu_sy7808_write(chip, 5, (value & 0xff) | ((channels & 4) << 5));
		if (rc < 0)
			goto out;
	}
	chip->mode = mode;
	chip->enabled_channels |= channels;
	rc = 0;
out:
	mutex_unlock(&chip->lock);
	return rc;
}

/* Stock 0x19cf18 updates its software mask before any I/O, even on error. */
int meizu_sy7808_disable(struct meizu_sy7808 *chip, u32 channels)
{
	u32 value = 0;
	int rc;

	if (!chip || !chip->io)
		return -EINVAL;
	mutex_lock(&chip->lock);
	chip->enabled_channels &= ~channels;
	rc = meizu_sy7808_read(chip, 1, &value);
	if (rc < 0)
		goto out;
	if (channels & 3) {
		rc = meizu_sy7808_write(chip, 1, value & ((channels & 3) ^ 0xff));
		if (rc < 0)
			goto out;
	}
	if (channels & 4) {
		rc = meizu_sy7808_read(chip, 5, &value);
		if (rc < 0)
			goto out;
		rc = meizu_sy7808_write(chip, 5, value & (((channels & 4) << 5) ^ 0xff));
		if (rc < 0)
			goto out;
	}
	if (!(chip->enabled_channels & 7)) {
		rc = meizu_sy7808_read(chip, 1, &value);
		if (rc < 0)
			goto out;
		rc = meizu_sy7808_write(chip, 1, value & 0xf3);
		if (rc < 0)
			goto out;
	}
	rc = 0;
out:
	mutex_unlock(&chip->lock);
	return rc;
}

/* Preserve the stock 32-bit products and reciprocal division instructions,
 * including clamps before subtract-one encoding. Errors log and continue.
 */
int meizu_sy7808_set_duty(struct meizu_sy7808 *chip, u32 mode, u32 duty)
{
	u32 value, product, high;

	if (!chip || !chip->io || (mode != 2 && mode != 3))
		return -EINVAL;
	mutex_lock(&chip->lock);
	if (mode == 2) {
		product = (duty * 5) & 0x7fffffff;
		value = ((u64)product * 0x30c30c31) >> 34;
		value = clamp_t(u32, value, 1, 72) - 1;
		meizu_sy7808_write(chip, 6, value);
		meizu_sy7808_write(chip, 7, value);
		meizu_sy7808_write(chip, 8, value);
	} else {
		product = duty * 1000;
		high = ((u64)product * 0xdcf6d64f) >> 32;
		value = (high + ((product - high) >> 1)) >> 15;
		value = clamp_t(u32, value, 1, 30) - 1;
		meizu_sy7808_write(chip, 3, value);
		meizu_sy7808_write(chip, 4, value);
		product = duty * 100;
		value = ((u64)product * 0x4a8eb527) >> 41;
		value = clamp_t(u32, value, 1, 60) - 1;
		meizu_sy7808_write(chip, 5, value);
	}
	mutex_unlock(&chip->lock);
	return 0;
}

/* leds_torch_write_reg: the original factory protocol is a register selector,
 * command, value, signed brightness and extra byte. The selector and command
 * tables below are decoded from the two ELF jump tables at 0x19b8c8/0x19ba20.
 * As in stock, individual bus errors are logged but do not abort the sequence.
 */
int meizu_sy7808_factory(struct meizu_sy7808 *chip, u8 selector,
		u8 command, u8 value, s32 brightness, u8 extra)
{
	static const u8 addresses[] = { 6, 7, 1, 10, 8, 5, 10 };
	u8 address = selector < ARRAY_SIZE(addresses) ? addresses[selector] : selector;

	if (!chip || !chip->io)
		return -EINVAL;
	if (selector == 3 || selector == 6) {
		if ((value & 0xfe) != 4)
			value = 26;
		else if (brightness > 1023) value = 122;
		else if (brightness > 511) value = 106;
		else if (brightness > 255) value = 90;
		else if (brightness > 127) value = 74;
		else if (brightness > 63) value = 58;
		else if (brightness > 31) value = 42;
		else value = brightness > 0 ? 26 : 10;
	}
	mutex_lock(&chip->lock);
	switch (command) {
	case 0: meizu_sy7808_write(chip, address, 0); break;
	case 1: case 25: meizu_sy7808_write(chip, address, 9); break;
	case 2: case 24: meizu_sy7808_write(chip, address, 10); break;
	case 3: meizu_sy7808_write(chip, address, 11); break;
	case 4: meizu_sy7808_write(chip, address, 1); break;
	case 5: meizu_sy7808_write(chip, address, 2); break;
	case 6: meizu_sy7808_write(chip, address, 3); break;
	case 7: case 8: meizu_sy7808_write(chip, address, extra); break;
	case 9: meizu_sy7808_write(chip, address, value); break;
	case 10: meizu_sy7808_write(chip, 5, 0); break;
	case 11: case 12:
		meizu_sy7808_write(chip, 1, command == 11 ? 8 : 11);
		meizu_sy7808_write(chip, 5, 128);
		break;
	case 13: case 14:
		meizu_sy7808_write(chip, 5, 128);
		meizu_sy7808_write(chip, 1, command == 13 ? 0 : 3);
		break;
	case 15: meizu_sy7808_write(chip, 8, extra); break;
	case 16: meizu_sy7808_write(chip, 10, value); break;
	case 17:
		meizu_sy7808_write(chip, 1, 9);
		meizu_sy7808_write(chip, address, 128);
		break;
	case 18:
		meizu_sy7808_write(chip, address, 10);
		meizu_sy7808_write(chip, 5, 128);
		break;
	case 20: case 21: case 22:
		meizu_sy7808_write(chip, 1, command == 20 ? 0 : command == 21 ? 10 : 9);
		meizu_sy7808_write(chip, address, 0);
		break;
	case 26: meizu_sy7808_write(chip, address, 8); break;
	default:
		CAM_ERR(CAM_FLASH, "Unknown Meizu factory command 0x%x", command);
		break;
	}
	mutex_unlock(&chip->lock);
	return 0;
}
