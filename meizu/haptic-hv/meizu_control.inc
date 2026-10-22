/* SPDX-License-Identifier: GPL-2.0-only */
/* Included by haptic_core.c: OEM callbacks share its RAM/RTP primitives. */
#include "meizu_calibration.h"
#include "meizu_effects.h"

static int mz_haptic_trigger_init(struct aw_haptic *aw,
		struct device_node *node)
{
	u8 waveform = 13;
	int attempt, ret;

	aw->meizu.trig2_gpio = of_get_named_gpio(node, "motor_trig2", 0);
	if (aw->meizu.trig2_gpio == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (!gpio_is_valid(aw->meizu.trig2_gpio))
		return 0;
	ret = devm_gpio_request(aw->dev, aw->meizu.trig2_gpio, "motor_trig2");
	if (ret) {
		aw->meizu.trig2_gpio = ret;
		return ret;
	}
	for (attempt = 0; attempt < 3; attempt++) {
		ret = gpio_direction_output(aw->meizu.trig2_gpio, 0);
		if (!ret)
			break;
		msleep(100);
	}
	if (!ret)
		msleep(40);
	/* TRG2 positive-edge waveform 13, enable, positive polarity. */
	haptic_hv_i2c_writes(aw, 0x15, &waveform, AW_I2C_BYTE_ONE);
	haptic_hv_i2c_write_bits(aw, 0x1c, (u8)~2, 2);
	haptic_hv_i2c_write_bits(aw, 0x1b, (u8)~4, 4);
	return 0;
}

static int mz_haptic_read_frequency(struct aw_haptic *aw, unsigned int mode)
{
	struct mz_haptic_calibration record;
	int ret = mz_haptic_read_calibration(&record);

	if (ret < 0)
		return ret;
	if (mode == 253) {
		mode = record.selected_chip;
		if (mode >= 1 && mode <= 3)
			aw->meizu.selected_chip = mode;
	}
	switch (mode) {
	case 1:
		return record.manual_f0;
	case 2:
		aw->f0_cali_data = record.factory_trim;
		return record.factory_f0_first;
	case 3:
		aw->f0_cali_data = record.user_trim;
		return record.user_f0_first;
	default:
		return 0;
	}
}

static int mz_haptic_calibrate_and_save(struct aw_haptic *aw, unsigned int mode,
		const char *buf, size_t count)
{
	struct mz_haptic_calibration record;
	long integer, fraction;
	u8 zero = 0;
	int first, second, ret;

	if (mode < 1 || mode > 3 || (mode == 1 && (!buf || count < 8)))
		return -EINVAL;
	mutex_lock(&aw->lock);
	ret = mz_haptic_read_calibration(&record);
	if (ret == -ENODATA)
		memset(&record, 0, sizeof(record));
	else if (ret < 0)
		goto out;
	record.selected_chip = mode;
	if (mode == 1) {
		/* Keep the original fixed offsets, not a generic floating-point
		 * parser: factory writes carry a three-byte prefix before F0.
		 */
		integer = simple_strtol(buf + 3, NULL, 0);
		fraction = simple_strtol(buf + 7, NULL, 0);
		if (integer > INT_MAX / 10 || integer < INT_MIN / 10 ||
		    fraction > INT_MAX || fraction < INT_MIN ||
		    (s64)integer * 10 + fraction > INT_MAX ||
		    (s64)integer * 10 + fraction < INT_MIN) {
			ret = -ERANGE;
			goto out;
		}
		aw->f0 = integer * 10 + fraction;
		record.manual_f0 = aw->f0;
	} else {
		ret = haptic_hv_i2c_writes(aw, 0x5b, &zero, 1);
		if (ret < 0)
			aw_err("clear trim before calibration failed: %d", ret);
		ret = f0_cali(aw);
		if (ret < 0)
			goto out;
		usleep_range(20000, 20500);
		first = aw->f0;
		ret = f0_cali(aw);
		if (ret < 0)
			goto out;
		usleep_range(20000, 20500);
		second = aw->f0;
		if (mode == 2) {
			record.factory_f0_first = first;
			record.factory_f0_second = second;
			record.factory_trim = aw->f0_cali_data;
		} else {
			record.user_f0_first = first;
			record.user_f0_second = second;
			record.user_trim = aw->f0_cali_data;
		}
	}
	ret = mz_haptic_write_calibration(&record);
out:
	mutex_unlock(&aw->lock);
	return ret;
}

static void mz_haptic_wave_combine(struct aw_haptic *aw,
		const u8 sequence[8], const u8 loop[5])
{
	int i;

	/* The stock helper writes all eight sequence registers and five loop
	 * registers, including 0x13, before stop then RAM playback.
	 */
	for (i = 0; i < 8; i++) {
		haptic_hv_i2c_writes(aw, 0x07 + i, (u8 *)&sequence[i], 1);
		if (i < 5)
			haptic_hv_i2c_writes(aw, 0x0f + i, (u8 *)&loop[i], 1);
	}
	aw->func->play_stop(aw);
	aw->func->play_wav_seq(aw, 1);
}

static void mz_haptic_trigger_pulse(struct aw_haptic *aw, u8 waveform)
{
	if (aw->meizu.trig2_waveform != waveform) {
		haptic_hv_i2c_writes(aw, 0x15, &waveform, 1);
		haptic_hv_i2c_write_bits(aw, 0x1c, ~0x02U, 0x02);
		aw->meizu.trig2_waveform = waveform;
	}
	if (gpio_is_valid(aw->meizu.trig2_gpio)) {
		gpio_set_value(aw->meizu.trig2_gpio, 1);
		udelay(1);
		gpio_set_value(aw->meizu.trig2_gpio, 0);
	}
}

static bool mz_haptic_timeout_delay(struct aw_haptic *aw, int duration)
{
	if (duration > 0)
		wait_event_timeout(aw->meizu.timeout_wait,
			READ_ONCE(aw->meizu.timeout_cancelled) ||
			READ_ONCE(aw->meizu.removed), msecs_to_jiffies(duration));
	return READ_ONCE(aw->meizu.timeout_cancelled) ||
		READ_ONCE(aw->meizu.removed);
}

static void mz_haptic_cancel_timeout(struct aw_haptic *aw)
{
	WRITE_ONCE(aw->meizu.timeout_cancelled, true);
	wake_up_all(&aw->meizu.timeout_wait);
	cancel_work_sync(&aw->meizu.timeout_work);
	WRITE_ONCE(aw->meizu.timeout_cancelled, false);
}

static void mz_haptic_timeout_work(struct work_struct *work)
{
	struct mz_haptic_runtime *runtime =
		container_of(work, struct mz_haptic_runtime, timeout_work);
	struct aw_haptic *aw = container_of(runtime, struct aw_haptic, meizu);
	int gain = aw->gain, loop, i = 0;

	mutex_lock(&aw->lock);
	if (gain < 0 || gain > 255)
		gain = 255;
	loop = runtime->play_time_ms > 50 || runtime->waveform == 19 ? 15 : 0;
	if (gain > 128) {
		gain = (gain >> 1) + 1;
		aw->gain = gain;
	}
	aw->func->upload_lra(aw, AW_F0_CALI_LRA);
	/* Original do/while deliberately plays once when play_count is zero. */
	do {
		if (mz_haptic_timeout_delay(aw, 0))
			goto stop;
		if (!runtime->play_time_ms ||
		    runtime->previous_waveform != runtime->waveform)
			aw->func->play_stop(aw);
		aw->func->play_mode(aw, AW_RAM_MODE);
		aw->func->set_wav_seq(aw, 0, runtime->waveform);
		aw->func->set_wav_seq(aw, 1, 0);
		aw->func->set_wav_loop(aw, 0, loop);
		aw->func->set_gain(aw, gain);
		aw->func->play_go(aw, true);
		if (!runtime->play_time_ms)
			aw->func->play_stop(aw);
		if (mz_haptic_timeout_delay(aw, runtime->stop_time_ms))
			goto stop;
	} while (++i < runtime->play_count);
	if (runtime->timeout_active) {
		mz_haptic_timeout_delay(aw, runtime->play_time_ms);
		runtime->timeout_active = false;
		aw->func->play_stop(aw);
	}
	mutex_unlock(&aw->lock);
	return;
stop:
	runtime->timeout_active = false;
	aw->func->play_stop(aw);
	mutex_unlock(&aw->lock);
}

static void mz_haptic_reset_repeat(struct aw_haptic *aw)
{
	aw->meizu.play_time_ms = 0;
	aw->meizu.stop_time_ms = 0;
	aw->meizu.play_count = 0;
}

static ssize_t aw8697_waveform_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	unsigned int waveform;
	if (sscanf(buf, "%u", &waveform) != 1)
		return -EINVAL;
	mutex_lock(&aw->lock);
	aw->meizu.previous_waveform = aw->meizu.waveform;
	aw->meizu.waveform = waveform;
	mutex_unlock(&aw->lock);
	return count;
}

static ssize_t aw8697_proline_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	int play = 0, stop = 0, repeats = 0;

	if (sscanf(buf, "%d %d %d", &play, &stop, &repeats) != 3)
		return -EINVAL;
	if (play < 0 || stop < 0 || repeats < 0)
		return -EINVAL;
	mz_haptic_cancel_timeout(aw);
	mutex_lock(&aw->lock);
	aw->meizu.waveform = 19;
	aw->meizu.play_time_ms = play;
	aw->meizu.stop_time_ms = stop;
	aw->meizu.play_count = repeats;
	if (play) {
		aw->meizu.timeout_active = true;
		aw->gain = 255;
	} else {
		aw->meizu.timeout_active = false;
		mz_haptic_reset_repeat(aw);
		aw->func->play_stop(aw);
	}
	mutex_unlock(&aw->lock);
	if (play)
		schedule_work(&aw->meizu.timeout_work);
	return count;
}

static ssize_t aw8697_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	int play, gain = 128, stop = READ_ONCE(aw->meizu.stop_time_ms);

	if (sscanf(buf, "%d %d %d", &play, &gain, &stop) < 1)
		return -EINVAL;
	if (play < 0 || stop < 0)
		return -EINVAL;
	mz_haptic_cancel_timeout(aw);
	mutex_lock(&aw->lock);
	aw->gain = gain;
	aw->meizu.play_time_ms = play;
	aw->meizu.stop_time_ms = stop;
	aw->meizu.play_count = 0;
	if (!play) {
		mz_haptic_reset_repeat(aw);
		aw->func->play_stop(aw);
		aw->func->set_gain(aw, 128);
	} else if (play <= 50 && aw->meizu.waveform == 1) {
		aw->func->set_gain(aw, gain);
		mz_haptic_trigger_pulse(aw, 1);
	} else {
		mutex_unlock(&aw->lock);
		schedule_work(&aw->meizu.timeout_work);
		return count;
	}
	mutex_unlock(&aw->lock);
	return count;
}

static ssize_t aw8697_mback_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "mback level: %d\n", aw->meizu.mback_level);
}

static ssize_t aw8697_cspress_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "cspress level: %d\n", aw->meizu.cspress_level);
}

static ssize_t mz_haptic_level_store(struct device *dev, const char *buf,
		size_t count, bool mback)
{
	static const u8 press_sequence[] = { 2, 4, 10 };
	static const u8 tap_sequence[] = { 1, 5, 6 };
	struct aw_haptic *aw = dev_get_drvdata(dev);
	struct mz_haptic_ram_effect *effect;
	unsigned int level;
	int play = 0;

	if (sscanf(buf, "%u %d", &level, &play) < 1)
		return -EINVAL;
	mutex_lock(&aw->lock);
	if (mback)
		aw->meizu.mback_level = level;
	else
		aw->meizu.cspress_level = level;
	mz_haptic_reset_repeat(aw);
	aw->func->play_stop(aw);
	if (aw->meizu.restore_default_gain)
		aw->func->set_gain(aw, 128);
	/* Original mutable rows: 30200 (mback), 30201 and 31023 (pressure). */
	effect = &mz_haptic_ram_effects[mback ? 23 : 24];
	if (level <= 2) {
		memset(effect->sequence, 0, sizeof(effect->sequence));
		effect->sequence[0] = mback ? 16 + level : press_sequence[level];
		if (!mback) {
			memset(mz_haptic_ram_effects[50].sequence, 0, 8);
			mz_haptic_ram_effects[50].sequence[0] = tap_sequence[level];
		}
	}
	if (mback)
		memset(effect->loop, 0, sizeof(effect->loop));
	if (play)
		mz_haptic_wave_combine(aw, effect->sequence, effect->loop);
	mutex_unlock(&aw->lock);
	return count;
}

static ssize_t aw8697_mback_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return mz_haptic_level_store(dev, buf, count, true);
}

static ssize_t aw8697_cspress_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return mz_haptic_level_store(dev, buf, count, false);
}

static ssize_t aw8697_on_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	unsigned int id;
	int index, trigger = -1, ret = kstrtouint(buf, 0, &id);

	if (ret)
		return ret;
	mutex_lock(&aw->lock);
	aw->func->upload_lra(aw, AW_F0_CALI_LRA);
	if (aw->meizu.restore_default_gain)
		aw->func->set_gain(aw, 128);
	switch (id) {
	case 0:
		goto out;
	case 21251:
	case 31010:
		mz_haptic_reset_repeat(aw);
		aw->func->play_stop(aw);
		goto out;
	case 20120: case 31021:
		trigger = 8;
		break;
	case 21000:
		trigger = 11;
		break;
	case 22560:
		trigger = 12;
		break;
	case 30900:
		trigger = 3;
		break;
	case 31008:
		trigger = 13;
		break;
	}
	if (trigger >= 0) {
		mz_haptic_trigger_pulse(aw, trigger);
		goto out;
	}
	aw->meizu.last_effect_id = id;
	index = mz_haptic_find_ram_effect(id);
	if (index >= 0)
		mz_haptic_wave_combine(aw, mz_haptic_ram_effects[index].sequence,
				mz_haptic_ram_effects[index].loop);
out:
	mutex_unlock(&aw->lock);
	return count;
}

static ssize_t mz_haptic_frequency_show(struct device *dev, char *buf,
		unsigned int mode)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	char text[6];
	int f0;

	if (mode != 1) {
		mutex_lock(&aw->lock);
		aw->func->get_lra_resistance(aw);
		mutex_unlock(&aw->lock);
		if (aw->lra < 5600 || aw->lra > 13000)
			return -1;
	}
	mutex_lock(&aw->lock);
	f0 = mz_haptic_read_frequency(aw, mode);
	mutex_unlock(&aw->lock);
	if (f0 < 0)
		return f0;
	text[0] = f0 / 1000 + '0';
	text[1] = f0 % 1000 / 100 + '0';
	text[2] = f0 % 100 / 10 + '0';
	text[3] = '.';
	text[4] = f0 % 10 + '0';
	text[5] = '\0';
	/* Preserve the six-byte sysfs result and original snprintf limit. */
	return snprintf(buf, 6, "%s\n", text);
}

static ssize_t aw8697_freq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return mz_haptic_frequency_show(dev, buf, 1);
}

static ssize_t aw8697_auto_freq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return mz_haptic_frequency_show(dev, buf, 2);
}

static ssize_t aw8697_usr_f0_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return mz_haptic_frequency_show(dev, buf, 3);
}

static ssize_t aw8697_freq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = mz_haptic_calibrate_and_save(dev_get_drvdata(dev), 1, buf, count);
	return ret < 0 ? ret : count;
}

static ssize_t mz_haptic_calibration_store(struct device *dev,
		const char *buf, size_t count, unsigned int mode)
{
	unsigned int enable;
	int ret = kstrtouint(buf, 0, &enable);

	if (ret)
		return ret;
	if (enable == 1) {
		ret = mz_haptic_calibrate_and_save(dev_get_drvdata(dev), mode, NULL, 0);
		if (ret < 0)
			return ret;
	}
	return count;
}

static ssize_t aw8697_auto_freq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return mz_haptic_calibration_store(dev, buf, count, 2);
}

static ssize_t aw8697_usr_f0_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return mz_haptic_calibration_store(dev, buf, count, 3);
}

static ssize_t aw8697_set_rtp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	unsigned int id;
	int index, ret = kstrtouint(buf, 0, &id);

	if (ret)
		return ret;
	cancel_work_sync(&aw->rtp_work);
	if (!(aw->flags & AW_FLAG_SKIP_INTERRUPTS))
		disable_irq(aw->meizu.irq);
	mutex_lock(&aw->lock);
	mz_haptic_reset_repeat(aw);
	aw->func->play_stop(aw);
	aw->func->set_rtp_aei(aw, false);
	if (!((id >= 5000 && id <= 5074) ||
	      (id >= 5100 && id <= 5110))) {
		aw->state = 0;
		goto out;
	}
	index = mz_haptic_find_rtp_effect(id);
	if (index < 0) {
		index = 3;
		id = 5003;
	}
	aw->rtp_file_num = index;
	aw->state = 1;
	aw->meizu.rtp_effect_id = id;
	strscpy(aw->meizu.rtp_filename, mz_haptic_rtp_effects[index].filename,
		 sizeof(aw->meizu.rtp_filename));
out:
	if (!(aw->flags & AW_FLAG_SKIP_INTERRUPTS))
		enable_irq(aw->meizu.irq);
	mutex_unlock(&aw->lock);
	return count;
}

static ssize_t aw8697_rtp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&aw->lock);
	ret = scnprintf(buf, PAGE_SIZE, "rtp play: %s\n", aw->meizu.rtp_filename);
	mutex_unlock(&aw->lock);
	return ret;
}

static ssize_t aw8697_rtp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw_haptic *aw = dev_get_drvdata(dev);
	unsigned int command;
	int ret = kstrtouint(buf, 0, &command);

	if (ret)
		return ret;
	if (command && command != 40021)
		return count;
	cancel_work_sync(&aw->rtp_work);
	if (!(aw->flags & AW_FLAG_SKIP_INTERRUPTS))
		disable_irq(aw->meizu.irq);
	mutex_lock(&aw->lock);
	if (command) {
		if (aw->meizu.restore_default_gain)
			aw->func->set_gain(aw, 128);
		aw->func->upload_lra(aw, AW_OSC_CALI_LRA);
		mz_haptic_reset_repeat(aw);
		aw->func->play_stop(aw);
		aw->func->set_rtp_aei(aw, false);
	} else {
		aw->state = 0;
	}
	/* The original stop branch leaked one IRQ-disable depth per write. */
	if (!(aw->flags & AW_FLAG_SKIP_INTERRUPTS))
		enable_irq(aw->meizu.irq);
	mutex_unlock(&aw->lock);
	schedule_work(&aw->rtp_work);
	return count;
}
