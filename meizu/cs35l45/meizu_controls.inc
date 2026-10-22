/* SPDX-License-Identifier: GPL-2.0-only */
/* M2391 mixer and stream protocol recovered from the original module. */
static const char * const channel_swap_texts[] = { "Off", "On" };
static SOC_ENUM_SINGLE_DECL(channel_swap_enum, SND_SOC_NOPM, 0, channel_swap_texts);
static const char * const external_switch_texts[] = { "SWITCH-rcv", "SWITCH-spk" };
static SOC_ENUM_SINGLE_DECL(external_switch_enum, SND_SOC_NOPM, 0, external_switch_texts);
static const char * const digital_pcm_volume_soft_ramp_text[] = {
	"Off", "0.5ms", "1ms", "2ms", "4ms", "8ms", "16ms", "32ms",
};
static SOC_ENUM_SINGLE_DECL(digital_pcm_volume_soft_ramp,
	CS35L45_AMP_PCM_CONTROL, 12, digital_pcm_volume_soft_ramp_text);
static const unsigned int cs35l45_pcm_sftramp_ms_double[] = { 0, 1, 2, 4, 8, 16, 32, 64 };

static int cs35l45_amp_register_dump_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = 0;
	return 0;
}

static int cs35l45_amp_register_dump_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);
	unsigned int reg, value = 0, maximum;
	int ret;

	if (!ucontrol->value.integer.value[0])
		return 0;
	maximum = ucontrol->value.integer.value[0] == 1 ? 0x12000 : 0x3c6efe8;
	for (reg = 0; reg <= maximum; reg += 4) {
		if (!cs35l45_readable_reg(cs35l45->dev, reg))
			continue;
		ret = regmap_read(cs35l45->regmap, reg, &value);
		if (ret)
			return ret;
		dev_err(cs35l45->dev, "%07x: %08x\n", reg, value);
		cond_resched();
	}
	return 0;
}

static int cs35l45_amp_active_status_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(
		snd_soc_kcontrol_component(kcontrol));
	unsigned int global = 0, pll = 0;

	regmap_read(cs35l45->regmap, CS35L45_GLOBAL_ENABLES, &global);
	regmap_read(cs35l45->regmap, CS35L45_REFCLK_INPUT, &pll);
	ucontrol->value.integer.value[0] = (global | (pll >> 16)) & 1;
	return 0;
}

/* The original status control is read-only in effect, despite its RW mode. */
static int cs35l45_amp_active_status_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int cs35l45_channel_swap_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(
		snd_soc_kcontrol_component(kcontrol));
	u32 value = 0;

	if (cs35l45->dsp.booted)
		wm_adsp_read_ctl(&cs35l45->dsp, "CH_BAL", WMFW_ADSP2_XM,
			CS35L45_ALGID, &value, sizeof(value));
	/* This is the original raw byte representation, not a host-endian DSP word. */
	ucontrol->value.integer.value[0] = value == 0x4000;
	return 0;
}

static int cs35l45_channel_swap_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(
		snd_soc_kcontrol_component(kcontrol));
	u32 value = ucontrol->value.integer.value[0] ? 0x4000 : 0;

	if (cs35l45->dsp.booted)
		wm_adsp_write_ctl(&cs35l45->dsp, "CH_BAL", WMFW_ADSP2_XM,
			CS35L45_ALGID, &value, sizeof(value));
	return 0;
}

static int cs35l45_external_switch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(
		snd_soc_kcontrol_component(kcontrol));

	ucontrol->value.integer.value[0] = gpio_is_valid(cs35l45->external_switch_gpio) ?
		gpio_get_value_cansleep(cs35l45->external_switch_gpio) : 0;
	return 0;
}

static int cs35l45_external_switch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(
		snd_soc_kcontrol_component(kcontrol));

	if (!gpio_is_valid(cs35l45->external_switch_gpio))
		return -EINVAL;
	gpio_set_value_cansleep(cs35l45->external_switch_gpio,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int cs35l45_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	unsigned int value = 0, volume, ramp;

	dev_info(cs35l45->dev, "%s: mute = %d, stream = %d\n", __func__, mute, stream);
	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;
	regmap_read(cs35l45->regmap, CS35L45_AMP_PCM_CONTROL, &value);
	volume = value & 0x7ff;
	dev_info(cs35l45->dev, "%s: reg = 0x%x, vol = 0x%x\n", __func__, value, volume);
	if (mute) {
		if (volume >= 1024 && volume < 1232)
			return 0;
		regmap_update_bits(cs35l45->regmap, CS35L45_AMP_PCM_CONTROL, 0x7ff, 1024);
		ramp = (value >> 12) & 15;
		if (ramp < ARRAY_SIZE(cs35l45_pcm_sftramp_ms_double))
			dev_info(cs35l45->dev, "%s: soft_ramp_ms = %u\n", __func__,
				9 * cs35l45_pcm_sftramp_ms_double[ramp]);
	} else if (volume >= 1024 && volume < 1232) {
		regmap_update_bits(cs35l45->regmap, CS35L45_AMP_PCM_CONTROL, 0x7ff, 0);
	}
	return 0;
}

static int cs35l45_dai_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2, 0x10, 0);
	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2, 0x01, 0);
	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2, 0x700, 0x200);
	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2, 0x04, 0);
	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2, 0x40, 0);
	return 0;
}

/* Stock shutdown has no hardware operation; DAPM owns the power transition. */
static void cs35l45_dai_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
}
