// SPDX-License-Identifier: GPL-2.0
/*
 * ASoC Driver for HiFiBerry DAC+ HD
 *
 * Author:	Joerg Schambacher, i2Audio GmbH for HiFiBerry
 *		Copyright 2020
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define DEBUG

//#include <stdio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/firmware.h>

#define TAS3251_PAGE		0x00
#define TAS3251_DIG_VOL_LEFT	0x3d
#define TAS3251_DIG_VOL_RIGHT	0x3e
#define TAS3251_POWER		0x02
#define TAS3251_BOOK		0x7f

#define TAS3251_DSPR		0x80

#define DEFAULT_RATE		44100

#define ALSA_NAME		"tas3251.1-004a"
#define ALSA_DAI_NAME		"tas3251-hifi"
/*
#define ALSA_NAME		"pcm512x.1-004c"
#define ALSA_DAI_NAME		"pcm512x-hifi"
*/

/* PPC3 commands */
#define CFG_META_DELAY		0xfe
#define CFG_META_BURST		0xfd
#define CFG_ASCII_TEXT		0xf0

struct brd_drv_data {
	struct regmap *regmap;
	struct clk *sclk;
};

static struct brd_drv_data drvdata;
static struct gpio_desc *reset_gpio;
static const unsigned int hb_dacplushd_rates[] = {
	96000, 48000, 88200, 44100,
};

static struct snd_pcm_hw_constraint_list hb_dacplushd_constraints = {
	.list = hb_dacplushd_rates,
	.count = ARRAY_SIZE(hb_dacplushd_rates),
};

/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void snd_tas3251hd_dacplushd_write_firmware(struct snd_soc_component *component)
{
	const struct firmware *fw;											// {size_t size; const u8 *data; void *priv;};
	u8 i = 0, ret, burst;

	ret = request_firmware_direct(&fw, "tas3251/ppc3_output.bin", component->dev);					// Firmware loader
	if ((ret < 0) || (fw->size < 2) || (fw->size & 1)) {								// always odd, last byte skipped
		dev_err(component->dev, "Error: Tas3251 Firmware missing or invalid\n");

	}
	else {
		dev_dbg(component->dev, "Tas3251 Firmware found\n");
		dev_dbg(component->dev, "Firmware length: %zd\n", fw->size / 2); 
	}

	snd_soc_component_update_bits(component, TAS3251_POWER, TAS3251_DSPR, 0);
	while (i < fw->size) {
		switch (fw->data[i]) {
		case CFG_META_DELAY:
			usleep_range((1000 * fw->data[i + 1]), (1000 * fw->data[i + 1]) + 10000);
			i++;
			break;
		case CFG_META_BURST:
			burst = 0;
			while (burst < (fw->data[i + 1] - 1)) {
				snd_soc_component_write(component,							// device
					fw->data[i + 2] + burst, fw->data[i + 3 + burst]);				// address, data 
				dev_dbg(component->dev,"address = 0x%02x, data = 0x%02x",
					(fw->data[i + 2] + burst), fw->data[i + 3 + burst]); 
				burst++;
			}
			i += (fw->data[i + 1] + 4);
			break;
		case CFG_ASCII_TEXT:											// skip n = fw->data[i + 1] - 1 ascii characters
			i +=  fw->data[i + 1];
			break;
		default:
			snd_soc_component_write(component, fw->data[i], fw->data[i + 1]);
			i++;
		}
		dev_dbg(component->dev, "0x%03x firmware { 0x%02x, 0x%02x }\n",
			i - 1 , fw->data[(i - 1)], fw->data[(i -1) + 1]);
	i++;
	}
	
	snd_soc_component_write(component,TAS3251_PAGE, 0x00);
	snd_soc_component_write(component,TAS3251_BOOK, 0x8c);
	snd_soc_component_write(component,TAS3251_PAGE, 0x23);
	dev_dbg(component->dev, "swap start");
	while (snd_soc_component_test_bits(component, 0x17, 0x01, 0x01))						// Wait for swap finished
		usleep_range(1e3, 2e3);
	dev_dbg(component->dev, "swap done");
	snd_soc_component_write(component,TAS3251_PAGE, 0x00);
	snd_soc_component_write(component,TAS3251_BOOK, 0x00);
	snd_soc_component_write(component,TAS3251_PAGE, 0x00);
//	snd_soc_component_update_bits(component,TAS3251_POWER,0x90,0x00);						// 0x02

	release_firmware(fw);
	usleep_range(2e5, 2.5e5);
}
*///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
*/
static int snd_rpi_hb_dacplushd_startup(struct snd_pcm_substream *substream)
{
//	struct snd_soc_pcm_runtime *rtd = substream->private_data;
//	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
	/* constraints for standard sample rates */
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&hb_dacplushd_constraints);
	return 0;
}

static void snd_tas3251hd_dacplushd_set_sclk(
		struct snd_soc_component *component,
		int sample_rate)
{
	if (!IS_ERR(drvdata.sclk))
		clk_set_rate(drvdata.sclk, sample_rate);
//	dev_dbg(component->dev, "Saample rate = %d", sample_rate);		///////////////////////////////////////////////////
}
/*
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int snd_tas3251hd_lowpass_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
//	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
//	struct glb_pool *glb_ptr = card->drvdata;
//	dev_dbg(NULL, "Lowpass_get called");
//	ucontrol->value.integer.value[0] = glb_ptr->set_lowpass;
	return 0;
}

static int snd_tas3251hd_lowpass_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_pcm_runtime *rtd;
//	struct glb_pool *glb_ptr = card->drvdata;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
//	dev_dbg(NULL, "Lowpass_put called");
//	return(snd_allo_piano_dsp_program(rtd,
//				glb_ptr->set_mode, glb_ptr->set_rate,
//				ucontrol->value.integer.value[0]));
	return 0;
}



static const char * const tas3251hd_dsp_low_pass_texts[] = {
	"60", "70", "80", "90", "100", "110", "120", "130",
	"140", "150", "160", "170", "180", "190", "200",
};

static SOC_ENUM_SINGLE_DECL(tas3251hd_enum,
		0, 0, tas3251hd_dsp_low_pass_texts);

*/
static const struct snd_kcontrol_new tas3251hd_controls[] = {
//	SOC_ENUM_EXT("Lowpass Route",
//		tas3251hd_enum,
//		snd_tas3251hd_lowpass_get,
//		snd_tas3251hd_lowpass_put),
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int snd_tas3251hd_dacplushd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai_link *dai = rtd->dai_link;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
//	const struct firmware *fw;									// {size_t size; const u8 *data; void *priv;};
	int ret;
//	const char *fw_names;
	struct snd_soc_card *card = rtd->card;

	ret = snd_soc_limit_volume(card, "Digital Playback Volume", 207);
	if (ret < 0)
		dev_warn(card->dev, "Failed to set volume limit: %d\n", ret);

	snd_soc_component_write(component, TAS3251_DIG_VOL_LEFT, 0x70);					// initial volume L
	snd_soc_component_write(component, TAS3251_DIG_VOL_RIGHT, 0x70);				// initial volume R
	dai->name = "TAS3251 HD";
	dai->stream_name = "TAS3251 HD HiFi";
	dai->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
		| SND_SOC_DAIFMT_CBM_CFM;

	/* allow only fixed 32 clock counts per channel */
	snd_soc_dai_set_bclk_ratio(cpu_dai, 32*2);
/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	if (device_property_read_string(card->dev, "firmwares", &fw_names))
		fw_names = "default";
		dev_info(card->dev, "firmwares = %s", fw_names);
*///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	return 0;
}

static void snd_tas3251hd_gpio_mute(struct snd_soc_card *card)
{
	if (reset_gpio) {
		dev_info(card->dev, "muting amp using GPIO %d\n", desc_to_gpio(reset_gpio));
		gpiod_set_value_cansleep(reset_gpio, 0);
	}
}

static void snd_tas3251hd_gpio_unmute(struct snd_soc_card *card)
{
	if (reset_gpio) {
		dev_info(card->dev, "un-muting amp using GPIO %d\n", desc_to_gpio(reset_gpio));
		gpiod_set_value_cansleep(reset_gpio, 1);
	}
}

static int snd_tas3251hd_set_bias_level(struct snd_soc_card *card,
	struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	codec_dai = asoc_rtd_to_codec(rtd, 0);

	if (dapm->dev != codec_dai->dev)
		return 0;

//	dev_dbg(card->dev, "Dai bias level = 0x%x", level);
//	dev_dbg(card->dev, "Dapm bias_level = 0x%x", dapm->bias_level);
	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level != SND_SOC_BIAS_STANDBY)
			break;

		/* UNMUTE AMP */
//		snd_tas3251hd_dacplushd_write_firmware();
		snd_tas3251hd_gpio_unmute(card);

		break;
	case SND_SOC_BIAS_STANDBY:
		if (dapm->bias_level != SND_SOC_BIAS_PREPARE)
			break;

		/* MUTE AMP */
		snd_tas3251hd_gpio_mute(card);

		break;
	default:
		break;
	}

	return 0;
}


static int snd_tas3251hd_dacplushd_hw_params(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
	snd_tas3251hd_dacplushd_set_sclk(component, params_rate(params));
	dev_dbg(component->dev, "Sample rate = %d", params_rate(params));		///////////////////////////////////////////////////

//	snd_soc_component_update_bits(component, TAS3251_POWER, 0x80, 0x80);




//	snd_tas3251hd_dacplushd_write_firmware(component);						// testing
//	snd_soc_component_update_bits(component, TAS3251_POWER, 0x80, 0x00);

	return ret;
}

/* machine stream operations */
static struct snd_soc_ops snd_tas3251hd_dacplushd_ops = {
	.startup = snd_rpi_hb_dacplushd_startup,
	.hw_params = snd_tas3251hd_dacplushd_hw_params,
};

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_CPU("bcm2708-i2s.0")),							// dai_name
	DAILINK_COMP_ARRAY(COMP_CODEC(ALSA_NAME, ALSA_DAI_NAME)),					// name, dai_name
	DAILINK_COMP_ARRAY(COMP_PLATFORM("bcm2708-i2s.0")));						// name


static struct snd_soc_dai_link snd_tas3251hd_dacplushd_dai[] = {
{
	.name		= "TAS3251 HD",
	.stream_name	= "TAS3251 HD HiFi",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &snd_tas3251hd_dacplushd_ops,
	.init		= snd_tas3251hd_dacplushd_init,
	SND_SOC_DAILINK_REG(hifi),
},
};

/* audio machine driver */
static struct snd_soc_card snd_tas3251hd_dacplushd = {
	.name		= "Tas3251HD",
	.driver_name	= "Tas3251HD",
	.owner		= THIS_MODULE,
	.dai_link	= snd_tas3251hd_dacplushd_dai,
	.num_links	= ARRAY_SIZE(snd_tas3251hd_dacplushd_dai),
	.controls	= tas3251hd_controls,
	.num_controls	= ARRAY_SIZE(tas3251hd_controls),
};

static int snd_tas3251hd_dacplushd_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
//	struct snd_soc_dai_link *dai = &snd_tas3251hd_dacplushd_dai[0];
	struct device_node *dev_node = dev->of_node;

	snd_tas3251hd_dacplushd.dev = &pdev->dev;

	if (pdev->dev.of_node) {
		struct device_node *i2s_node;
//		struct snd_soc_card *card = &snd_tas3251hd_dacplushd;
		struct snd_soc_dai_link *dai;

		dai = &snd_tas3251hd_dacplushd_dai[0];
		i2s_node = of_parse_phandle(pdev->dev.of_node,
			"i2s-controller", 0);

		if (i2s_node) {
			dai->cpus->of_node = i2s_node;
			dai->platforms->of_node = i2s_node;
			dai->cpus->dai_name = NULL;
			dai->platforms->name = NULL;
		} else {
			return -EPROBE_DEFER;
		}

	}
//	reset_gpio = devm_gpiod_get_optional(&pdev->dev, "reset",
	reset_gpio = devm_gpiod_get(&pdev->dev, "reset",
		GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio)) {
		ret = PTR_ERR(reset_gpio);
		dev_err(&pdev->dev,
			"Failed to get reset gpio: %d\n", ret);
		return ret;
	}
	snd_tas3251hd_dacplushd.set_bias_level =
		snd_tas3251hd_set_bias_level;
	ret = devm_snd_soc_register_card(&pdev->dev,
		&snd_tas3251hd_dacplushd);
	if (ret && ret != -EPROBE_DEFER) {
		dev_err(&pdev->dev,
		"snd_soc_register_card() failed: %d\n", ret);
		return ret;
	}
	if (ret == -EPROBE_DEFER)
		return ret;

	dev_set_drvdata(dev, &drvdata);
	if (dev_node == NULL) {
		dev_err(&pdev->dev, "Device tree node not found\n");
		return -ENODEV;
	}

	drvdata.sclk = devm_clk_get(dev, NULL);
	if (IS_ERR(drvdata.sclk)) {
		drvdata.sclk = ERR_PTR(-ENOENT);
		return -ENODEV;
	}

	clk_set_rate(drvdata.sclk, DEFAULT_RATE);

	snd_tas3251hd_gpio_mute(&snd_tas3251hd_dacplushd);

	return ret;
}

static int snd_tas3251hd_dacplushd_remove(struct platform_device *pdev)
{
	if (IS_ERR(reset_gpio))
		return -EINVAL;

	/* put DAC into RESET and release GPIO */
	gpiod_set_value(reset_gpio, 0);
	gpiod_put(reset_gpio);

	return 0;
}

static const struct of_device_id snd_tas3251hd_dacplushd_of_match[] = {
	{ .compatible = "ti,snd-tas3251hd", },
	{},
};

MODULE_DEVICE_TABLE(of, snd_tas3251hd_dacplushd_of_match);

static struct platform_driver snd_tas3251hd_dacplushd_driver = {
	.driver = {
		.name   = "snd_tas3251hd_dacplushd",
		.owner  = THIS_MODULE,
		.of_match_table = snd_tas3251hd_dacplushd_of_match,
	},
	.probe          = snd_tas3251hd_dacplushd_probe,
	.remove		= snd_tas3251hd_dacplushd_remove,
};

module_platform_driver(snd_tas3251hd_dacplushd_driver);

MODULE_AUTHOR("Joerg Schambacher <joerg@i2audio.com>");
MODULE_AUTHOR("JP van Coolwijk <jpvc36@gmail.com>");
MODULE_DESCRIPTION("ASoC Driver for HiFiBerry DAC+ HD");
MODULE_LICENSE("GPL v2");