// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TAS3251 ASoC codec driver by JP van Coolwijk, based on
 * PCM179X ASoC codec driver
 *
 * Copyright (c) Amarula Solutions B.V. 2013
 *
 *     Michael Trimarchi <michael@amarulasolutions.com>
 */

#define DEBUG

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/firmware.h>

#define DEFAULT_RATE			44100

#define TAS3251_RESET			0x01
#define TAS3251_POWER			0x02
#define TAS3251_MUTE			0x03
#define TAS3251_PLL_EN			0x04
#define TAS3251_SCLK_LRCLK_CFG		0x09
#define TAS3251_MASTER_MODE		0x0c
#define TAS3251_PLL_DSP_REF		0x0d
#define TAS3251_MASTER_CLKDIV_1		0x20
#define TAS3251_MASTER_CLKDIV_2		0x21
#define TAS3251_ERROR_DETECT		0x25
#define TAS3251_I2S_1			0x28
#define TAS3251_I2S_2			0x29
#define TAS3251_DIG_VOL_LEFT		0x3d
#define TAS3251_DIG_VOL_RIGHT		0x3e
#define TAS3251_DIG_MUTE_1		0x3f
#define TAS3251_CLOCK_STATUS		0x5f

#define TAS3251_SAMPLERATES		{44100, 48000, 88200, 96000}			
#define TAS3251_FORMATS			(SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE |\
					SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S20_3LE |\
					SNDRV_PCM_FMTBIT_S16_LE)
#define TAS3251_RATES			(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
					SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define TAS3251_RSTM			0x10
#define TAS3251_RSTR			0x01
#define TAS3251_RQPD			0x01
#define TAS3251_RQST			0x10
#define TAS3251_DSPR			0x80
#define TAS3251_PLLE			0x01
#define TAS3251_MUTE_MASK		0x11
#define TAS3251_CLK_CFG_MASK		0x91
#define TAS3251_CLK_OE			0x11
#define TAS3251_CLKDIV_EN		0x03
#define TAS3251_PLL_DSP_REF_MASK	0x17
#define TAS3251_SREF_SCLK		0x10
#define TAS3251_SREF_MCLK		0x00
#define TAS3251_SDSP_MCLK		0x03
#define TAS3251_IDCH_ERR		0x08
#define TAS3251_IPLK_ERR		0x01
#define TAS3251_AFMT			0x30
#define TAS3251_ALEN			0x03
#define TAS3251_CDST6_ERR		0x40

/* PPC3 commands */
#define CFG_META_DELAY			0xfe
#define CFG_META_BURST			0xfd
#define CFG_ASCII_TEXT			0xf0

static const struct reg_default tas3251_reg_defaults[] = {
	{ TAS3251_RESET, 0x00 },		{ TAS3251_POWER, 0x80 },
	{ TAS3251_MUTE, 0x00 },			{ TAS3251_PLL_EN, 0x01 },
	{ TAS3251_MASTER_CLKDIV_1, 0x00 },	{ TAS3251_MASTER_CLKDIV_2, 0x00 },
	{ TAS3251_DIG_VOL_LEFT, 0x30 },		{ TAS3251_DIG_VOL_RIGHT, 0x30 },
};

int samplerates[4] = TAS3251_SAMPLERATES;

struct tas3251_private {
	struct regmap *regmap;
	unsigned int format, rate;
//	struct gpio_desc *gpio_mute_n, *gpio_pdn_n;
	struct mutex lock;
	uint8_t *dsp_cfg_data[4];
	int dsp_cfg_len[4];
	const char *fw_name;
	int previous_rate;
};

static void tas3251_get_firmware(struct snd_soc_component *component) {
	int i, ret;
	char filename[128];
	const struct firmware *fw;
	struct tas3251_private *priv = snd_soc_component_get_drvdata(component);
	mutex_lock(&priv->lock);
	if (device_property_read_string(component->dev, "firmware", &priv->fw_name))
		priv->fw_name = "default";
//		dev_info(component->dev, "Firmware name = %s", priv->fw_name);
	for (i = 0; i < 4 ; i++) {
		snprintf(filename, sizeof(filename), "tas3251/tas3251_%s_%d.bin", priv->fw_name, samplerates[i]);
//		snprintf(filename, sizeof(filename), "tas3251/tas3251_%s_%d.bin", priv->fw_name, priv->samplerates[i]);
		ret = request_firmware_direct(&fw, filename, component->dev);
		if (!ret) {
			priv->dsp_cfg_len[i] = fw->size;
			priv->dsp_cfg_data[i] = devm_kmemdup(component->dev, fw->data, fw->size, GFP_KERNEL);
//			dev_dbg(component->dev, "Firmware = %s\n", filename);
//			dev_dbg(component->dev, "Firmware length: %zd\n", fw->size / 2); 
			if (!priv->dsp_cfg_data[i]) {
				dev_err(component->dev, "firmware is not loaded, using minimal config\n");
				priv->dsp_cfg_len[i] = 0;
				ret = 1;
			}
			if ((fw->size < 2) || (fw->size & 1)) {
				dev_err(component->dev, "firmware is invalid, using minimal config\n");
				priv->dsp_cfg_len[i] = 0;
				ret = 1;
			}
		} else {
			dev_err(component->dev, "firmware not found, using minimal config\n");
			priv->dsp_cfg_len[i] = 0;
		}
		if (ret) {
			dev_err(component->dev,"  Please provide valid firmware in /lib/firmware/tas3251\n");
			dev_err(component->dev,"  Format: tas3251_<fw_name>_<rate>.bin");
		}
	release_firmware(fw);
	}
	mutex_unlock(&priv->lock);
}

static void tas3251_write_firmware(struct snd_soc_component *component) {
	struct tas3251_private *priv = snd_soc_component_get_drvdata(component);
	int i = 0, cfg = 0;
//	int samplerates[4] = {44100, 48000, 88200, 96000};
	mutex_lock(&priv->lock);
	dev_dbg(component->dev, "Previous rate is %d", priv->previous_rate);
//	dev_dbg(component->dev, "Sample rate = %d\n", priv->rate);
	regmap_update_bits(priv->regmap, TAS3251_POWER, TAS3251_DSPR, 0);
	while ((samplerates[cfg] != priv->rate) && (cfg < 4)) cfg++ ;
//	while ((priv->samplerates[cfg] != priv->rate) && (cfg < 4)) cfg++ ;
	if (priv->previous_rate == priv->rate) {
		dev_dbg(component->dev, "writing dsp config not necessary");
		goto skip_write;
	}
	if (priv->dsp_cfg_len[cfg] == 0) {
		dev_dbg(component->dev, "writing dsp config not possible");
		goto skip_write;
	}
	dev_dbg(component->dev, "start writing dsp config");
	while (i < priv->dsp_cfg_len[cfg]) {
		switch (priv->dsp_cfg_data[cfg][i]) {
		case CFG_META_DELAY:
			usleep_range((1000 * priv->dsp_cfg_data[cfg][i + 1]), (1000 * priv->dsp_cfg_data[cfg][i + 1]) + 10000);
			i++;
			break;
		case CFG_META_BURST:
			regmap_bulk_write(priv->regmap, priv->dsp_cfg_data[cfg][i + 2], &priv->dsp_cfg_data[cfg][i + 3], priv->dsp_cfg_data[cfg][i + 1] - 1);
			i +=  priv->dsp_cfg_data[cfg][i + 1];
			break;
		case CFG_ASCII_TEXT:									// skip n = fw->data[i + 1] - 1 ascii characters
			i +=  priv->dsp_cfg_data[cfg][i + 1];
			break;
		default:
			regmap_write(priv->regmap, priv->dsp_cfg_data[cfg][i], priv->dsp_cfg_data[cfg][i + 1]);
			i++;
		}
	i++;
	}
	dev_info(component->dev, "DSP config \"tas3251_%s_%d.bin\" written\n", priv->fw_name, priv->rate);
skip_write:
	priv->previous_rate = priv->rate;
	mutex_unlock(&priv->lock);
}

static int tas3251_set_dai_fmt(struct snd_soc_dai *codec_dai,
                             unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;
	struct tas3251_private *priv = snd_soc_component_get_drvdata(component);
	u8 val, ret, offset = 0x00;

//	dev_dbg(component->dev, "Format = 0x%x\n", format);
	priv->format = format;
//	regmap_update_bits(priv->regmap, TAS3251_POWER,
//		TAS3251_DSPR | TAS3251_RQST | TAS3251_RQPD, TAS3251_DSPR | TAS3251_RQST);

	switch (priv->format & SND_SOC_DAIFMT_FORMAT_MASK) {						// 0x000f
	case SND_SOC_DAIFMT_I2S:									// 0x0001
		dev_dbg(component->dev, "DAI format is I2S");
		val = 0x00;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:									// 0x0002
		dev_dbg(component->dev, "DAI format is Right Justified");
		val = 0x20;
		break;
	case SND_SOC_DAIFMT_LEFT_J:									// 0x0003
		dev_dbg(component->dev, "DAI format is Left Justified");
		val = 0x30;
		break;
	case SND_SOC_DAIFMT_DSP_A:									// 0x0004
		offset = 0x01;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_B:									// 0x0005
		dev_dbg(component->dev, "DAI format is DSP, SCLK offset = %d", offset);
		val = 0x10;
		break;
	default:
		dev_err(component->dev, "Unsupported DAI format\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, TAS3251_I2S_1, TAS3251_AFMT, val << 4);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data format: %d\n", ret);
		return ret;
	}

	ret = regmap_write(priv->regmap, TAS3251_I2S_2, offset);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data offset: %x\n", offset);
		return ret;
	}

	switch (format & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {						// 0xf000
		case SND_SOC_DAIFMT_CBC_CFC:								// 0x4000
		dev_dbg(component->dev, "Codec is I2S consumer\n");
		regmap_update_bits(priv->regmap, TAS3251_PLL_EN, TAS3251_PLLE, TAS3251_PLLE);		// 0x04, 0x01, 0x01
		regmap_update_bits(priv->regmap, TAS3251_PLL_DSP_REF,					// 0x0d
			TAS3251_PLL_DSP_REF_MASK, TAS3251_SREF_SCLK);					// 0x17, 0x10
		regmap_update_bits(priv->regmap, TAS3251_ERROR_DETECT,					// 0x25
			TAS3251_IDCH_ERR | TAS3251_IPLK_ERR, TAS3251_IDCH_ERR | 0);			// 0x08
		regmap_update_bits(priv->regmap, TAS3251_SCLK_LRCLK_CFG,				// 0x09
			TAS3251_CLK_CFG_MASK, 0);							// 0x91, 0x00
		regmap_update_bits(priv->regmap, TAS3251_MASTER_MODE,					// 0x0c
			TAS3251_CLKDIV_EN, 0);								// 0x03, 0
		break;

		case SND_SOC_DAIFMT_CBP_CFP:								// 0x1000
		dev_dbg(component->dev, "Codec is I2S producer\n");
		regmap_update_bits(priv->regmap, TAS3251_PLL_EN, TAS3251_PLLE,0);			// 0x04, 0x01, 0
//		regmap_update_bits(priv->regmap, TAS3251_PLL_EN, TAS3251_PLLE, TAS3251_PLLE);		// 0x04, 0x01, 0x01
		regmap_update_bits(priv->regmap, TAS3251_PLL_DSP_REF,					// 0x0d
//			TAS3251_PLL_DSP_REF_MASK, TAS3251_SREF_MCLK | TAS3251_SDSP_MCLK);		// 0x17, 0x00 | 0x03
			TAS3251_PLL_DSP_REF_MASK, TAS3251_SREF_MCLK);					// 0x17, 0x00
		if (regmap_test_bits(priv->regmap, TAS3251_CLOCK_STATUS, TAS3251_CDST6_ERR)) {		// 0x5f, 0x40
			dev_err(component->dev,
				"Need MCLK for master mode:\n        45.1585 / 49.152 MHz\n");
		return -EIO;
		}
		regmap_update_bits(priv->regmap, TAS3251_ERROR_DETECT,					// 0x25
			TAS3251_IDCH_ERR | TAS3251_IPLK_ERR, 0 | TAS3251_IPLK_ERR);			// 0x08 | 0x01, 0 | 0x01
		regmap_update_bits(priv->regmap, TAS3251_SCLK_LRCLK_CFG,				// 0x09
			TAS3251_CLK_CFG_MASK, TAS3251_CLK_OE);						// 0x91, 0x11
		regmap_write(priv->regmap, TAS3251_MASTER_CLKDIV_1, 0x0f);				// 0x20, 0x0f
		regmap_write(priv->regmap, TAS3251_MASTER_CLKDIV_2, 0x3f);				// 0x21, 0x3f
		regmap_update_bits(priv->regmap, TAS3251_MASTER_MODE,					// 0x0c
			TAS3251_CLKDIV_EN, TAS3251_CLKDIV_EN);						// 0x03, 0x03

		break;

		default:
		dev_err(component->dev, "Format unsupported\n");
		return -EINVAL;
	}
	priv->rate = DEFAULT_RATE;
	tas3251_get_firmware(component);
	tas3251_write_firmware(component);
	return 0;
}

static int tas3251_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_private *priv = snd_soc_component_get_drvdata(component);
	int ret;
	if (!mute) regmap_update_bits(priv->regmap, TAS3251_POWER,					// 0x02
		TAS3251_DSPR | TAS3251_RQST, 0);							// 0x80 | 0x10, 0
	usleep_range(1e3, 2e3);
	dev_dbg(component->dev, "Mute = 0x%x\n", mute);
	ret = regmap_update_bits(priv->regmap, TAS3251_MUTE,						// 0x03
				 TAS3251_MUTE_MASK, mute ? TAS3251_MUTE_MASK : 0);			// 0x11, 0x11 : 0
	usleep_range(1e3, 2e3);
	if (mute) regmap_update_bits(priv->regmap, TAS3251_POWER,					// 0x02
		TAS3251_DSPR | TAS3251_RQST, TAS3251_DSPR | TAS3251_RQST);				// 0x80 | 0x10, 0x90 : 0
	if (ret < 0)
		return ret;
	return 0;
}

static int tas3251_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_private *priv = snd_soc_component_get_drvdata(component);
	u8 val, ret = 0;

	priv->rate = params_rate(params);
/*
	dev_dbg(component->dev, "hw_params %u Hz, %u channels, %u bit\n",
		params_rate(params),
		params_channels(params),
		params_width(params));
*/
	switch (params_rate(params)) {
		case 96000:
		case 88200:
			val = 0x07;
			break;
		case 48000:
		case 44100:
			val = 0x0f;
			break;
/*
		case 32000:
			val = 0x17;
			break;
*/
		default:
			return -EINVAL;
		}

		if ((priv->format & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) == SND_SOC_DAIFMT_CBP_CFP) {
			ret = regmap_write(priv->regmap, TAS3251_MASTER_CLKDIV_1, val);
		}
		if (ret != 0) {
			dev_err(component->dev, "Failed to set clock divider: %d\n", ret);
			return ret;
	}
//	dev_dbg(component->dev, "Clkdiv set\n");
	switch (params_width(params)) {
		case 16:
			val = 0x00;
			break;
		case 20:
			val = 0x01;
			break;
		case 24:
			val = 0x02;
			break;
		case 32:
			val = 0x03;
			break;
		default:
			dev_err(component->dev, "Invalid width\n");
			return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, TAS3251_I2S_1, TAS3251_ALEN, val << 0);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data format: %d\n", ret);
		return ret;
	}
//	dev_dbg(component->dev, "End of tas3251_hw_params\n");
//	tas3251_get_firmware(component);
	tas3251_write_firmware(component);
	return 0;
}

static const struct snd_soc_dai_ops tas3251_dai_ops = {
	.set_fmt	= tas3251_set_dai_fmt,
	.hw_params	= tas3251_hw_params,
	.mute_stream	= tas3251_mute,
	.no_capture_mute = 1,
};

static const DECLARE_TLV_DB_SCALE(tas3251_dac_tlv, -10350, 50, 1);

static const struct snd_kcontrol_new tas3251_controls[] = {
	SOC_DOUBLE_R_TLV("Digital Playback Volume", TAS3251_DIG_VOL_LEFT,
		 	 TAS3251_DIG_VOL_RIGHT, 0, 255, 1,
			 tas3251_dac_tlv),
};

static const struct snd_soc_dapm_widget tas3251_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("IOUTL+"),
SND_SOC_DAPM_OUTPUT("IOUTL-"),
SND_SOC_DAPM_OUTPUT("IOUTR+"),
SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route tas3251_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

static struct snd_soc_dai_driver tas3251_dai = {
	.name = "tas3251-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TAS3251_RATES,
		.formats = TAS3251_FORMATS,
		},
	.ops = &tas3251_dai_ops,
};

const struct regmap_config tas3251_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.cache_type		= REGCACHE_NONE,
};
EXPORT_SYMBOL_GPL(tas3251_regmap_config);

static const struct snd_soc_component_driver soc_component_dev_tas3251 = {
	.controls		= tas3251_controls,
	.num_controls		= ARRAY_SIZE(tas3251_controls),
	.dapm_widgets		= tas3251_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas3251_dapm_widgets),
	.dapm_routes		= tas3251_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(tas3251_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

int tas3251_common_init(struct device *dev, struct regmap *regmap)
{
	struct tas3251_private *tas3251;
//	int samplerates[4] = {44100, 48000, 88200, 96000};
	tas3251 = devm_kzalloc(dev, sizeof(struct tas3251_private),
				GFP_KERNEL);
	if (!tas3251)
		return -ENOMEM;

	tas3251->regmap = regmap;
	dev_set_drvdata(dev, tas3251);

//	tas3251->samplerates = {44100, 48000, 88200, 96000};
//	tas3251->samplerates[0] = 44100;
//	*tas3251->samplerates = *samplerates;

	return devm_snd_soc_register_component(dev,
			&soc_component_dev_tas3251, &tas3251_dai, 1);
}
EXPORT_SYMBOL_GPL(tas3251_common_init);

static int tas3251_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &tas3251_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	regmap_update_bits(regmap, TAS3251_RESET,						// 0x01
		TAS3251_RSTM | TAS3251_RSTR, TAS3251_RSTM | TAS3251_RSTR);			// 0x10 | 0x01, 0x11
//		TAS3251_RSTM | TAS3251_RSTR, TAS3251_RSTM | 0);					// 0x10 | 0x01, 0x10
	regmap_update_bits(regmap, TAS3251_MUTE,						// 0x03
		TAS3251_MUTE_MASK, TAS3251_MUTE_MASK);						// 0x3f
	regmap_write(regmap, TAS3251_DIG_MUTE_1, 0xbb);						// VNDF, VNDS, VNUF, VNUS
	return tas3251_common_init(&client->dev, regmap);
}

#ifdef CONFIG_OF
static const struct of_device_id tas3251_of_match[] = {
	{ .compatible = "ti,tas3251", },
	{ }
};
MODULE_DEVICE_TABLE(of, tas3251_of_match);
#endif

static const struct i2c_device_id tas3251_i2c_ids[] = {
	{ "tas3251", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas3251_i2c_ids);

static struct i2c_driver tas3251_i2c_driver = {
	.driver = {
		.name	= "tas3251",
		.of_match_table = of_match_ptr(tas3251_of_match),
	},
	.id_table	= tas3251_i2c_ids,
	.probe_new	= tas3251_i2c_probe,
};

module_i2c_driver(tas3251_i2c_driver);

MODULE_DESCRIPTION("ASoC TAS3251 driver");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_AUTHOR("JP van Coolwijk <jpvc36@gmail.com>");
MODULE_LICENSE("GPL");
