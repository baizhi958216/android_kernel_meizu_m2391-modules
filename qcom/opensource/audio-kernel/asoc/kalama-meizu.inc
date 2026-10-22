/* SPDX-License-Identifier: GPL-2.0-only */
/* Original M2391 CS35L45 links and USB-C analog jack protocol. */
#include <linux/usb/ucsi_glink.h>
#include <sound/jack.h>

static struct snd_soc_codec_conf cs35l45_codec_conf[] = {
	{ .dlc = { .name = "cs35l45.0-0031" }, .name_prefix = "SPK" },
	{ .dlc = { .name = "cs35l45.0-0030" }, .name_prefix = "RCV" },
};
SND_SOC_DAILINK_DEFS(secondary_smartpa_aif,
	DAILINK_COMP_ARRAY(COMP_CPU("snd-soc-dummy-dai")),
	DAILINK_COMP_ARRAY(COMP_CODEC("cs35l45.0-0030", "cs35l45"),
		COMP_CODEC("cs35l45.0-0031", "cs35l45")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("snd-soc-dummy")));

static int cs35l45_init(struct snd_soc_pcm_runtime *rtd)
{
	static const char * const pins[] = {
		"Playback", "Capture", "ASP_RX1", "ASP_RX2", "ASP_TX1", "ASP_TX2", "SPK", "AP",
	};
	struct snd_soc_component *component;
	int i, pin;

	for_each_rtd_components(rtd, i, component) {
		if (!component)
			break;
		if (!component->driver->name)
			continue;
		if (!strcmp(component->driver->name, "cs35l45"))
			for (pin = 0; pin < ARRAY_SIZE(pins); pin++)
				snd_soc_dapm_ignore_suspend(snd_soc_component_get_dapm(component), pins[pin]);
	}
	return 0;
}
static int cs35l45_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec;
	unsigned int width, clock;
	int i, ret;

	ret = msm_common_snd_hw_params(substream, params);
	if (ret)
		return ret;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		width = 32;
		break;
	default:
		width = 16;
		break;
	}
	clock = width * params_rate(params) * 2;
	for_each_rtd_codec_dais(rtd, i, codec) {
		ret = snd_soc_dai_set_fmt(codec, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS);
		if (ret < 0)
			return ret;
		ret = snd_soc_component_set_sysclk(codec->component, 0, 0, clock, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;
		ret = snd_soc_dai_set_sysclk(codec, 0, clock, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;
	}
	return ret;
}
static const struct snd_soc_ops cs35l45_be_ops = {
	.startup = msm_common_snd_startup,
	.hw_params = cs35l45_hw_params,
	.shutdown = msm_common_snd_shutdown,
};

static struct {
	struct snd_soc_card *card;
	struct snd_soc_jack jack;
	struct notifier_block notifier;
	struct work_struct work;
	atomic_t state;
	bool active;
} usbc_analog;
static DEFINE_MUTEX(usbc_analog_lock);

static void usbc_analog_work_fn(struct work_struct *work)
{
	if (smp_load_acquire(&usbc_analog.active))
		snd_soc_jack_report(&usbc_analog.jack,
			atomic_read(&usbc_analog.state) ? SND_JACK_LINEOUT : 0,
			SND_JACK_HEADSET | SND_JACK_LINEOUT);
}
static int usbc_event_changed(struct notifier_block *nb, unsigned long event, void *data)
{
	const struct ucsi_glink_constat_info *info = data;
	int state;

	if (!info || !smp_load_acquire(&usbc_analog.active))
		return NOTIFY_DONE;
	state = info->acc;
	if ((state == 0 || state == 1) && atomic_xchg(&usbc_analog.state, state) != state)
		schedule_work(&usbc_analog.work);
	return NOTIFY_DONE;
}
static int usbc_analog_detect_init(struct snd_soc_card *card)
{
	int ret;

	mutex_lock(&usbc_analog_lock);
	if (usbc_analog.card) {
		ret = usbc_analog.card == card ? 0 : -EBUSY;
		goto unlock;
	}
	INIT_WORK(&usbc_analog.work, usbc_analog_work_fn);
	atomic_set(&usbc_analog.state, 0);
	ret = snd_soc_card_jack_new(card, "USBC Jack", SND_JACK_HEADSET | SND_JACK_LINEOUT,
		&usbc_analog.jack, NULL, 0);
	if (ret)
		goto unlock;
	usbc_analog.card = card;
	usbc_analog.notifier.notifier_call = usbc_event_changed;

unlock:
	mutex_unlock(&usbc_analog_lock);
	return ret;
}
/* The card remove callback drains this pair before ALSA frees the jack. */
static void usbc_analog_detect_start(struct snd_soc_card *card)
{
	int ret;

	mutex_lock(&usbc_analog_lock);
	if (usbc_analog.card == card) {
		smp_store_release(&usbc_analog.active, true);
		ret = register_ucsi_glink_notifier(&usbc_analog.notifier);
		if (ret) {
			smp_store_release(&usbc_analog.active, false);
			cancel_work_sync(&usbc_analog.work);
		}
	}
	mutex_unlock(&usbc_analog_lock);
}
static void usbc_analog_detect_deinit(struct snd_soc_card *card)
{
	mutex_lock(&usbc_analog_lock);
	if (card && usbc_analog.card == card) {
		if (smp_load_acquire(&usbc_analog.active)) {
			smp_store_release(&usbc_analog.active, false);
			unregister_ucsi_glink_notifier(&usbc_analog.notifier);
		}
		cancel_work_sync(&usbc_analog.work);
		usbc_analog.card = NULL;
	}
	mutex_unlock(&usbc_analog_lock);
}

static int meizu_card_remove(struct snd_soc_card *card)
{
	usbc_analog_detect_deinit(card);
	return 0;
}
