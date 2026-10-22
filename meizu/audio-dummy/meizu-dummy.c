// SPDX-License-Identifier: GPL-2.0-only
/* Original Meizu dummy ASoC endpoint: two digital DAIs, no hardware controls. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/soc.h>

#define MEIZU_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | \
	SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | \
	SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_driver meizu_stub_dais[] = {
	{
		.name = "meizu-stub-rx",
		.playback = {
			.stream_name = "Playback",
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rates = MEIZU_RATES,
			.channels_min = 1,
			.channels_max = 8,
		},
	}, {
		.name = "meizu-stub-tx",
		.capture = {
			.stream_name = "Record",
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
			.rates = MEIZU_RATES,
			.channels_min = 1,
			.channels_max = 8,
		},
	},
};

static const struct snd_soc_component_driver soc_meizu_stub = {
	.name = "meizu-stub-codec",
};

static int meizu_stub_dev_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "dev name %s\n", dev_name(&pdev->dev));
	return snd_soc_register_component(&pdev->dev, &soc_meizu_stub,
		meizu_stub_dais, ARRAY_SIZE(meizu_stub_dais));
}

static int meizu_stub_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id meizu_stub_codec_dt_match[] = {
	{ .compatible = "qcom,meizu-stub-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, meizu_stub_codec_dt_match);

static struct platform_driver meizu_stub_driver = {
	.driver = {
		.name = "meizu-stub-codec",
		.owner = THIS_MODULE,
		.of_match_table = meizu_stub_codec_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = meizu_stub_dev_probe,
	.remove = meizu_stub_dev_remove,
};
module_platform_driver(meizu_stub_driver);
MODULE_DESCRIPTION("Meizu dummy audio codec");
MODULE_LICENSE("GPL v2");
