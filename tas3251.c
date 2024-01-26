// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the TAS3251 CODECs
 *
 * Author:	JPv Coolwijk <jpvc36@gmail.com>
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gcd.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "tas3251.h"

#define TAS3251_NUM_SUPPLIES 5
static const char * const tas3251_supply_names[TAS3251_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
	"CPVDD",
	"GVDD",
	"PVDD",
};

struct tas3251_priv {
	struct regmap *regmap;
	struct clk *sclk;
	struct regulator_bulk_data supplies[TAS3251_NUM_SUPPLIES];
	struct notifier_block supply_nb[TAS3251_NUM_SUPPLIES];
	int fmt;
	int pll_in;
	int pll_out;
	int pll_r;
	int pll_j;
	int pll_d;
	int pll_p;
	unsigned long real_pll;
	unsigned long overclock_pll;
	unsigned long overclock_dac;
	unsigned long overclock_dsp;
	int mute;
	struct mutex mutex;
	unsigned int bclk_ratio;
};

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define TAS3251_REGULATOR_EVENT(n) \
static int tas3251_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct tas3251_priv *tas3251 = container_of(nb, struct tas3251_priv, \
						    supply_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(tas3251->regmap);	\
		regcache_cache_only(tas3251->regmap, true);	\
	} \
	return 0; \
}

TAS3251_REGULATOR_EVENT(0)
TAS3251_REGULATOR_EVENT(1)
TAS3251_REGULATOR_EVENT(2)
TAS3251_REGULATOR_EVENT(3)
TAS3251_REGULATOR_EVENT(4)

static const struct reg_default tas3251_reg_defaults[] = {
	{ TAS3251_RESET,             0x00 },	//reg 1
	{ TAS3251_POWER,             0x80 },	//reg 2
	{ TAS3251_MUTE,              0x00 },	//reg 3
	{ TAS3251_DSP,               0x01 },	//reg 7
	{ TAS3251_PLL_DSP_REF,       0x00 },	//reg 13
	{ TAS3251_OSR_DAC_REF,       0x00 },	//reg 14
	{ TAS3251_NCP_REF,           0x00 },	//reg 15
	{ TAS3251_DAC_ROUTING,       0x11 },	//reg 42
	{ TAS3251_DSP_PROGRAM,       0x01 },	//reg 43
	{ TAS3251_CLKDET,            0x00 },	//reg 44
	{ TAS3251_AUTO_MUTE,         0x00 },	//reg 59
	{ TAS3251_ERROR_DETECT,      0x00 },	//reg 37
	{ TAS3251_DIGITAL_VOLUME_1,  0x00 },	//reg 60
	{ TAS3251_DIGITAL_VOLUME_2,  0x30 },	//reg 61
	{ TAS3251_DIGITAL_VOLUME_3,  0x30 },	//reg 62
	{ TAS3251_DIGITAL_MUTE_1,    0x33 },	//reg 63
	{ TAS3251_DIGITAL_MUTE_2,    0x10 },	//reg 64
	{ TAS3251_DIGITAL_MUTE_3,    0x07 },	//reg 65
	{ TAS3251_OUTPUT_AMPLITUDE,  0x00 },	//page 1 reg 1
	{ TAS3251_ANALOG_GAIN_CTRL,  0x00 },	//page 1 reg 2
	{ TAS3251_ANALOG_MUTE_CTRL,  0x01 },	//page 1 reg 6
	{ TAS3251_ANALOG_GAIN_BOOST, 0x00 },	//page 1 reg 7
	{ TAS3251_VCOM_CTRL_2,       0x01 },	//page 1 reg 9
	{ TAS3251_SCLK_LRCLK_CFG,    0x00 },	//reg 9
	{ TAS3251_MASTER_MODE,       0x01 },	//reg 12
	{ TAS3251_GPIO_DACIN,        0x00 },	//reg 16
	{ TAS3251_GPIO_NCPIN,        0x00 },	//reg 17
	{ TAS3251_GPIO_PLLIN,        0x00 },	//reg 18
	{ TAS3251_PLL_COEFF_0,       0x00 },	//reg 20
	{ TAS3251_PLL_COEFF_1,       0x08 },	//reg 21
	{ TAS3251_PLL_COEFF_2,       0x00 },	//reg 22
	{ TAS3251_PLL_COEFF_3,       0x00 },	//reg 23
	{ TAS3251_PLL_COEFF_4,       0x00 },	//reg 24
	{ TAS3251_DSP_CLKDIV,        0x00 },	//reg 27
	{ TAS3251_DAC_CLKDIV,        0x01 },	//reg 28
	{ TAS3251_NCP_CLKDIV,        0x01 },	//reg 29
	{ TAS3251_OSR_CLKDIV,        0x01 },	//reg 30
	{ TAS3251_MASTER_CLKDIV_1,   0x00 },	//reg 32
	{ TAS3251_MASTER_CLKDIV_2,   0x00 },	//reg 33
	{ TAS3251_FS_SPEED_MODE,     0x00 },	//reg 34
	{ TAS3251_I2S_1,             0x02 },	//reg 40
	{ TAS3251_I2S_2,             0x00 },	//reg 41
};

static bool tas3251_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAS3251_RESET:
	case TAS3251_POWER:
	case TAS3251_MUTE:
	case TAS3251_PLL_EN:
	case TAS3251_I2C_PAGE_AUTO_INC:
	case TAS3251_DSP:
	case TAS3251_GPIO_EN:
	case TAS3251_SCLK_LRCLK_CFG:
	case TAS3251_MASTER_MODE:
	case TAS3251_PLL_DSP_REF:
	case TAS3251_OSR_DAC_REF:
	case TAS3251_GPIO_DACIN:
	case TAS3251_GPIO_NCPIN:
	case TAS3251_GPIO_PLLIN:
	case TAS3251_PLL_COEFF_0:
	case TAS3251_PLL_COEFF_1:
	case TAS3251_PLL_COEFF_2:
	case TAS3251_PLL_COEFF_3:
	case TAS3251_PLL_COEFF_4:
	case TAS3251_DSP_CLKDIV:
	case TAS3251_DAC_CLKDIV:
	case TAS3251_NCP_CLKDIV:
	case TAS3251_OSR_CLKDIV:
	case TAS3251_MASTER_CLKDIV_1:
	case TAS3251_MASTER_CLKDIV_2:
	case TAS3251_FS_SPEED_MODE:
	case TAS3251_I2S_1:
	case TAS3251_I2S_2:
	case TAS3251_DAC_ROUTING:
	case TAS3251_DSP_PROGRAM:
	case TAS3251_CLKDET:
	case TAS3251_AUTO_MUTE:
	case TAS3251_DIGITAL_VOLUME_1:
	case TAS3251_DIGITAL_VOLUME_2:
	case TAS3251_DIGITAL_VOLUME_3:
	case TAS3251_DIGITAL_MUTE_1:
	case TAS3251_DIGITAL_MUTE_2:
	case TAS3251_DIGITAL_MUTE_3:
	case TAS3251_GPIO_SDOUT:
	case TAS3251_GPIO_CONTROL_1:
	case TAS3251_GPIO_CONTROL_2:
	case TAS3251_RATE_DET_1:
	case TAS3251_RATE_DET_2:
	case TAS3251_RATE_DET_3:
	case TAS3251_RATE_DET_4:
	case TAS3251_CLOCK_STATUS:
	case TAS3251_ANALOG_MUTE_DET:
	case TAS3251_GPIN:
	case TAS3251_DIGITAL_MUTE_DET:
	case TAS3251_OUTPUT_AMPLITUDE:
	case TAS3251_ANALOG_GAIN_CTRL:
	case TAS3251_ANALOG_MUTE_CTRL:
	case TAS3251_ANALOG_GAIN_BOOST:
	case TAS3251_VCOM_CTRL_2:
	case TAS3251_FLEX_A:			//(253) reg 63
	case TAS3251_FLEX_B:			//(253) reg 64
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static bool tas3251_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAS3251_PLL_EN:
	case TAS3251_RATE_DET_1:
	case TAS3251_RATE_DET_2:
	case TAS3251_RATE_DET_3:
	case TAS3251_RATE_DET_4:
	case TAS3251_CLOCK_STATUS:
	case TAS3251_ANALOG_MUTE_DET:
	case TAS3251_GPIN:
	case TAS3251_DIGITAL_MUTE_DET:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static int tas3251_overclock_pll_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = tas3251->overclock_pll;
	return 0;
}

static int tas3251_overclock_pll_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		return -EBUSY;
	}

	tas3251->overclock_pll = ucontrol->value.integer.value[0];
	return 0;
}

static int tas3251_overclock_dsp_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = tas3251->overclock_dsp;
	return 0;
}

static int tas3251_overclock_dsp_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		return -EBUSY;
	}

	tas3251->overclock_dsp = ucontrol->value.integer.value[0];
	return 0;
}

static int tas3251_overclock_dac_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = tas3251->overclock_dac;
	return 0;
}

static int tas3251_overclock_dac_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		return -EBUSY;
	}

	tas3251->overclock_dac = ucontrol->value.integer.value[0];
	return 0;
}

static const DECLARE_TLV_DB_SCALE(digital_tlv, -10350, 50, 1);
static const DECLARE_TLV_DB_SCALE(analog_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, 0, 80, 0);

static const char * const tas3251_clk_missing_text[] = {
	"1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s"
};

static const struct soc_enum tas3251_clk_missing =
	SOC_ENUM_SINGLE(TAS3251_CLKDET, 0,  8, tas3251_clk_missing_text);

static const char * const tas3251_autom_text[] = {
	"21ms", "106ms", "213ms", "533ms", "1.07s", "2.13s", "5.33s", "10.66s"
};

static const struct soc_enum tas3251_autom_l =
	SOC_ENUM_SINGLE(TAS3251_AUTO_MUTE, TAS3251_ATML_SHIFT, 8,
			tas3251_autom_text);

static const struct soc_enum tas3251_autom_r =
	SOC_ENUM_SINGLE(TAS3251_AUTO_MUTE, TAS3251_ATMR_SHIFT, 8,
			tas3251_autom_text);

static const char * const tas3251_ramp_rate_text[] = {
	"1 sample/update", "2 samples/update", "4 samples/update",
	"Immediate"
};

static const struct soc_enum tas3251_vndf =
	SOC_ENUM_SINGLE(TAS3251_DIGITAL_MUTE_1, TAS3251_VNDF_SHIFT, 4,
			tas3251_ramp_rate_text);

static const struct soc_enum tas3251_vnuf =
	SOC_ENUM_SINGLE(TAS3251_DIGITAL_MUTE_1, TAS3251_VNUF_SHIFT, 4,
			tas3251_ramp_rate_text);

static const struct soc_enum tas3251_vedf =
	SOC_ENUM_SINGLE(TAS3251_DIGITAL_MUTE_2, TAS3251_VEDF_SHIFT, 4,
			tas3251_ramp_rate_text);

static const char * const tas3251_ramp_step_text[] = {
	"4dB/step", "2dB/step", "1dB/step", "0.5dB/step"
};

static const struct soc_enum tas3251_vnds =
	SOC_ENUM_SINGLE(TAS3251_DIGITAL_MUTE_1, TAS3251_VNDS_SHIFT, 4,
			tas3251_ramp_step_text);

static const struct soc_enum tas3251_vnus =
	SOC_ENUM_SINGLE(TAS3251_DIGITAL_MUTE_1, TAS3251_VNUS_SHIFT, 4,
			tas3251_ramp_step_text);

static const struct soc_enum tas3251_veds =
	SOC_ENUM_SINGLE(TAS3251_DIGITAL_MUTE_2, TAS3251_VEDS_SHIFT, 4,
			tas3251_ramp_step_text);

static int tas3251_update_mute(struct tas3251_priv *tas3251)
{
	return regmap_update_bits(
		tas3251->regmap, TAS3251_MUTE, TAS3251_RQML | TAS3251_RQMR,
		(!!(tas3251->mute & 0x5) << TAS3251_RQML_SHIFT)
		| (!!(tas3251->mute & 0x3) << TAS3251_RQMR_SHIFT));
}

static int tas3251_digital_playback_switch_get(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	mutex_lock(&tas3251->mutex);
	ucontrol->value.integer.value[0] = !(tas3251->mute & 0x4);
	ucontrol->value.integer.value[1] = !(tas3251->mute & 0x2);
	mutex_unlock(&tas3251->mutex);

	return 0;
}

static int tas3251_digital_playback_switch_put(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	int ret, changed = 0;

	mutex_lock(&tas3251->mutex);

	if ((tas3251->mute & 0x4) == (ucontrol->value.integer.value[0] << 2)) {
		tas3251->mute ^= 0x4;
		changed = 1;
	}
	if ((tas3251->mute & 0x2) == (ucontrol->value.integer.value[1] << 1)) {
		tas3251->mute ^= 0x2;
		changed = 1;
	}

	if (changed) {
		ret = tas3251_update_mute(tas3251);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to update digital mute: %d\n", ret);
			mutex_unlock(&tas3251->mutex);
			return ret;
		}
	}

	mutex_unlock(&tas3251->mutex);

	return changed;
}

static const struct snd_kcontrol_new tas3251_controls[] = {
SOC_DOUBLE_R_TLV("Digital Playback Volume", TAS3251_DIGITAL_VOLUME_2,
		 TAS3251_DIGITAL_VOLUME_3, 0, 255, 1, digital_tlv),
SOC_DOUBLE_TLV("Analogue Playback Volume", TAS3251_ANALOG_GAIN_CTRL,
	       TAS3251_LAGN_SHIFT, TAS3251_RAGN_SHIFT, 1, 1, analog_tlv),
SOC_DOUBLE_TLV("Analogue Playback Boost Volume", TAS3251_ANALOG_GAIN_BOOST,
	       TAS3251_AGBL_SHIFT, TAS3251_AGBR_SHIFT, 1, 0, boost_tlv),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Playback Switch",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_ctl_boolean_stereo_info,
	.get = tas3251_digital_playback_switch_get,
	.put = tas3251_digital_playback_switch_put
},

SOC_SINGLE("Deemphasis Switch", TAS3251_DSP, TAS3251_DEMP_SHIFT, 1, 1),
SOC_ENUM("Clock Missing Period", tas3251_clk_missing),
SOC_ENUM("Auto Mute Time Left", tas3251_autom_l),
SOC_ENUM("Auto Mute Time Right", tas3251_autom_r),
SOC_SINGLE("Auto Mute Mono Switch", TAS3251_DIGITAL_MUTE_3,
	   TAS3251_ACTL_SHIFT, 1, 0),
SOC_DOUBLE("Auto Mute Switch", TAS3251_DIGITAL_MUTE_3, TAS3251_AMLE_SHIFT,
	   TAS3251_AMRE_SHIFT, 1, 0),

SOC_ENUM("Volume Ramp Down Rate", tas3251_vndf),
SOC_ENUM("Volume Ramp Down Step", tas3251_vnds),
SOC_ENUM("Volume Ramp Up Rate", tas3251_vnuf),
SOC_ENUM("Volume Ramp Up Step", tas3251_vnus),
SOC_ENUM("Volume Ramp Down Emergency Rate", tas3251_vedf),
SOC_ENUM("Volume Ramp Down Emergency Step", tas3251_veds),

SOC_SINGLE_EXT("Max Overclock PLL", SND_SOC_NOPM, 0, 20, 0,
	       tas3251_overclock_pll_get, tas3251_overclock_pll_put),
SOC_SINGLE_EXT("Max Overclock DSP", SND_SOC_NOPM, 0, 40, 0,
	       tas3251_overclock_dsp_get, tas3251_overclock_dsp_put),
SOC_SINGLE_EXT("Max Overclock DAC", SND_SOC_NOPM, 0, 40, 0,
	       tas3251_overclock_dac_get, tas3251_overclock_dac_put),
};

static const struct snd_soc_dapm_widget tas3251_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_OUTPUT("OUTL"),
SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route tas3251_dapm_routes[] = {
	{ "DACL", NULL, "Playback" },
	{ "DACR", NULL, "Playback" },

	{ "OUTL", NULL, "DACL" },
	{ "OUTR", NULL, "DACR" },
};

static unsigned long tas3251_pll_max(struct tas3251_priv *tas3251)
{
	return 25000000 + 25000000 * tas3251->overclock_pll / 100;
}

static unsigned long tas3251_dsp_max(struct tas3251_priv *tas3251)
{
	return 50000000 + 50000000 * tas3251->overclock_dsp / 100;
}

static unsigned long tas3251_dac_max(struct tas3251_priv *tas3251,
				     unsigned long rate)
{
	return rate + rate * tas3251->overclock_dac / 100;
}

static unsigned long tas3251_sck_max(struct tas3251_priv *tas3251)
{
	if (!tas3251->pll_out)
		return 25000000;
	return tas3251_pll_max(tas3251);
}

static unsigned long tas3251_ncp_target(struct tas3251_priv *tas3251,
					unsigned long dac_rate)
{
	/*
	 * If the DAC is not actually overclocked, use the good old
	 * NCP target rate...
	 */
	if (dac_rate <= 6144000)
		return 1536000;
	/*
	 * ...but if the DAC is in fact overclocked, bump the NCP target
	 * rate to get the recommended dividers even when overclocking.
	 */
	return tas3251_dac_max(tas3251, 1536000);
}

static const u32 tas3251_dai_rates[] = {
	32000, 44100, 48000, 88200, 96000,
};

static const struct snd_pcm_hw_constraint_list constraints_slave = {
	.count = ARRAY_SIZE(tas3251_dai_rates),
	.list  = tas3251_dai_rates,
};


static int tas3251_hw_rule_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct tas3251_priv *tas3251 = rule->private;
	struct snd_interval ranges[2];
	int frame_size;

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0)
		return frame_size;

	switch (frame_size) {
	case 32:
		/* No hole when the frame size is 32. */
		return 0;
	case 48:
	case 64:
		/* There is only one hole in the range of supported
		 * rates, but it moves with the frame size.
		 */
		memset(ranges, 0, sizeof(ranges));
		ranges[0].min = 32000;
		ranges[0].max = tas3251_sck_max(tas3251) / frame_size / 2;
		ranges[1].min = DIV_ROUND_UP(16000000, frame_size);
		ranges[1].max = 96000;
		break;
	default:
		return -EINVAL;
	}

	return snd_interval_ranges(hw_param_interval(params, rule->var),
				   ARRAY_SIZE(ranges), ranges, 0);
}

static int tas3251_dai_startup_master(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;
	struct snd_pcm_hw_constraint_ratnums *constraints_no_pll;
	struct snd_ratnum *rats_no_pll;

	if (IS_ERR(tas3251->sclk)) {
		dev_err(dev, "Need SCLK for master mode: %ld\n",
			PTR_ERR(tas3251->sclk));
		return PTR_ERR(tas3251->sclk);
	}

	if (tas3251->pll_out)
		return snd_pcm_hw_rule_add(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_RATE,
					   tas3251_hw_rule_rate,
					   tas3251,
					   SNDRV_PCM_HW_PARAM_FRAME_BITS,
					   SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	constraints_no_pll = devm_kzalloc(dev, sizeof(*constraints_no_pll),
					  GFP_KERNEL);
	if (!constraints_no_pll)
		return -ENOMEM;
	constraints_no_pll->nrats = 1;
	rats_no_pll = devm_kzalloc(dev, sizeof(*rats_no_pll), GFP_KERNEL);
	if (!rats_no_pll)
		return -ENOMEM;
	constraints_no_pll->rats = rats_no_pll;
	rats_no_pll->num = clk_get_rate(tas3251->sclk) / 64;
	rats_no_pll->den_min = 1;
	rats_no_pll->den_max = 128;
	rats_no_pll->den_step = 1;

	return snd_pcm_hw_constraint_ratnums(substream->runtime, 0,
					     SNDRV_PCM_HW_PARAM_RATE,
					     constraints_no_pll);
}

static int tas3251_dai_startup_slave(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;
	struct regmap *regmap = tas3251->regmap;

	if (IS_ERR(tas3251->sclk)) {
		dev_info(dev, "No SCLK, using BCLK: %ld\n",
			 PTR_ERR(tas3251->sclk));

		/* Disable reporting of missing SCLK as an error */
		regmap_update_bits(regmap, TAS3251_ERROR_DETECT,
				   TAS3251_IDCH, TAS3251_IDCH);

		/* Switch PLL input to BCLK */
		regmap_update_bits(regmap, TAS3251_PLL_DSP_REF,				// reg 13
					TAS3251_SREF | TAS3251_SDSP,
					TAS3251_SREF_SCLK | TAS3251_SDSP_PLL);
	}

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &constraints_slave);
}

static int tas3251_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	switch (tas3251->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
	case SND_SOC_DAIFMT_CBP_CFC:
		return tas3251_dai_startup_master(substream, dai);

	case SND_SOC_DAIFMT_CBC_CFC:
		return tas3251_dai_startup_slave(substream, dai);

	default:
		return -EINVAL;
	}
}

static int tas3251_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct tas3251_priv *tas3251 = dev_get_drvdata(component->dev);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		ret = regmap_update_bits(tas3251->regmap, TAS3251_POWER,
					 TAS3251_RQST, 0);
		if (ret != 0) {
			dev_err(component->dev, "Failed to remove standby: %d\n",
				ret);
			return ret;
		}
		break;

	case SND_SOC_BIAS_OFF:
		ret = regmap_update_bits(tas3251->regmap, TAS3251_POWER,
					 TAS3251_RQST, TAS3251_RQST);
		if (ret != 0) {
			dev_err(component->dev, "Failed to request standby: %d\n",
				ret);
			return ret;
		}
		break;
	}

	return 0;
}

static unsigned long tas3251_find_sck(struct snd_soc_dai *dai,
				      unsigned long bclk_rate)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	unsigned long sck_rate;
	int pow2;

	/* 64 MHz <= pll_rate <= 100 MHz, VREF mode */
	/* 16 MHz <= sck_rate <=  25 MHz, VREF mode */

	/* select sck_rate as a multiple of bclk_rate but still with
	 * as many factors of 2 as possible, as that makes it easier
	 * to find a fast DAC rate
	 */
	pow2 = 1 << fls((tas3251_pll_max(tas3251) - 16000000) / bclk_rate);
	for (; pow2; pow2 >>= 1) {
		sck_rate = rounddown(tas3251_pll_max(tas3251),
				     bclk_rate * pow2);
		if (sck_rate >= 16000000)
			break;
	}
	if (!pow2) {
		dev_err(dev, "Impossible to generate a suitable SCK\n");
		return 0;
	}

	dev_dbg(dev, "sck_rate %lu\n", sck_rate);
	return sck_rate;
}

/* pll_rate = pllin_rate * R * J.D / P
 * 1 <= R <= 16
 * 1 <= J <= 63
 * 0 <= D <= 9999
 * 1 <= P <= 15
 * 64 MHz <= pll_rate <= 100 MHz
 * if D == 0
 *     1 MHz <= pllin_rate / P <= 20 MHz
 * else if D > 0
 *     6.667 MHz <= pllin_rate / P <= 20 MHz
 *     4 <= J <= 11
 *     R = 1
 */
static int tas3251_find_pll_coeff(struct snd_soc_dai *dai,
				  unsigned long pllin_rate,
				  unsigned long pll_rate)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	unsigned long common;
	int R, J, D, P;
	unsigned long K; /* 10000 * J.D */
	unsigned long num;
	unsigned long den;

	common = gcd(pll_rate, pllin_rate);
	dev_dbg(dev, "pll %lu pllin %lu common %lu\n",
		pll_rate, pllin_rate, common);
	num = pll_rate / common;
	den = pllin_rate / common;

	/* pllin_rate / P (or here, den) cannot be greater than 20 MHz */
	if (pllin_rate / den > 20000000 && num < 8) {
		num *= DIV_ROUND_UP(pllin_rate / den, 20000000);
		den *= DIV_ROUND_UP(pllin_rate / den, 20000000);
	}
	dev_dbg(dev, "num / den = %lu / %lu\n", num, den);

	P = den;
	if (den <= 15 && num <= 16 * 63
	    && 1000000 <= pllin_rate / P && pllin_rate / P <= 20000000) {
		/* Try the case with D = 0 */
		D = 0;
		/* factor 'num' into J and R, such that R <= 16 and J <= 63 */
		for (R = 16; R; R--) {
			if (num % R)
				continue;
			J = num / R;
			if (J == 0 || J > 63)
				continue;

			dev_dbg(dev, "R * J / P = %d * %d / %d\n", R, J, P);
			tas3251->real_pll = pll_rate;
			goto done;
		}
		/* no luck */
	}

	R = 1;

	if (num > 0xffffffffUL / 10000)
		goto fallback;

	/* Try to find an exact pll_rate using the D > 0 case */
	common = gcd(10000 * num, den);
	num = 10000 * num / common;
	den /= common;
	dev_dbg(dev, "num %lu den %lu common %lu\n", num, den, common);

	for (P = den; P <= 15; P++) {
		if (pllin_rate / P < 6667000 || 200000000 < pllin_rate / P)
			continue;
		if (num * P % den)
			continue;
		K = num * P / den;
		/* J == 12 is ok if D == 0 */
		if (K < 40000 || K > 120000)
			continue;

		J = K / 10000;
		D = K % 10000;
		dev_dbg(dev, "J.D / P = %d.%04d / %d\n", J, D, P);
		tas3251->real_pll = pll_rate;
		goto done;
	}

	/* Fall back to an approximate pll_rate */

fallback:
	/* find smallest possible P */
	P = DIV_ROUND_UP(pllin_rate, 20000000);
	if (!P)
		P = 1;
	else if (P > 15) {
		dev_err(dev, "Need a slower clock as pll-input\n");
		return -EINVAL;
	}
	if (pllin_rate / P < 6667000) {
		dev_err(dev, "Need a faster clock as pll-input\n");
		return -EINVAL;
	}
	K = DIV_ROUND_CLOSEST_ULL(10000ULL * pll_rate * P, pllin_rate);
	if (K < 40000)
		K = 40000;
	/* J == 12 is ok if D == 0 */
	if (K > 120000)
		K = 120000;
	J = K / 10000;
	D = K % 10000;
	dev_dbg(dev, "J.D / P ~ %d.%04d / %d\n", J, D, P);
	tas3251->real_pll = DIV_ROUND_DOWN_ULL((u64)K * pllin_rate, 10000 * P);

done:
	tas3251->pll_r = R;
	tas3251->pll_j = J;
	tas3251->pll_d = D;
	tas3251->pll_p = P;
	return 0;
}

static unsigned long tas3251_pllin_dac_rate(struct snd_soc_dai *dai,
					    unsigned long osr_rate,
					    unsigned long pllin_rate)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	unsigned long dac_rate;

	if (!tas3251->pll_out)
		return 0; /* no PLL to bypass, force SCK as DAC input */

	if (pllin_rate % osr_rate)
		return 0; /* futile, quit early */

	/* run DAC no faster than 6144000 Hz */
	for (dac_rate = rounddown(tas3251_dac_max(tas3251, 6144000), osr_rate);
	     dac_rate;
	     dac_rate -= osr_rate) {

		if (pllin_rate / dac_rate > 128)
			return 0; /* DAC divider would be too big */

		if (!(pllin_rate % dac_rate))
			return dac_rate;

		dac_rate -= osr_rate;
	}

	return 0;
}

static int tas3251_set_dividers(struct snd_soc_dai *dai,
				struct snd_pcm_hw_params *params)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	unsigned long pllin_rate = 0;
	unsigned long pll_rate;
	unsigned long sck_rate;
	unsigned long mck_rate;
	unsigned long bclk_rate;
	unsigned long sample_rate;
	unsigned long osr_rate;
	unsigned long dacsrc_rate;
	int bclk_div;
	int lrclk_div;
	int dsp_div;
	int dac_div;
	unsigned long dac_rate;
	int ncp_div;
	int osr_div;
	int ret;
	int idac;
	int fssp;
	int gpio;

	if (tas3251->bclk_ratio > 0) {
		lrclk_div = tas3251->bclk_ratio;
	} else {
		lrclk_div = snd_soc_params_to_frame_size(params);

		if (lrclk_div == 0) {
			dev_err(dev, "No LRCLK?\n");
			return -EINVAL;
		}
	}

	if (!tas3251->pll_out) {
		sck_rate = clk_get_rate(tas3251->sclk);
		bclk_rate = params_rate(params) * lrclk_div;
		bclk_div = DIV_ROUND_CLOSEST(sck_rate, bclk_rate);

		mck_rate = sck_rate;
	} else {
		ret = snd_soc_params_to_bclk(params);
		if (ret < 0) {
			dev_err(dev, "Failed to find suitable BCLK: %d\n", ret);
			return ret;
		}
		if (ret == 0) {
			dev_err(dev, "No BCLK?\n");
			return -EINVAL;
		}
		bclk_rate = ret;

		pllin_rate = clk_get_rate(tas3251->sclk);

		sck_rate = tas3251_find_sck(dai, bclk_rate);
		if (!sck_rate)
			return -EINVAL;
		pll_rate = 4 * sck_rate;

		ret = tas3251_find_pll_coeff(dai, pllin_rate, pll_rate);
		if (ret != 0)
			return ret;

		ret = regmap_write(tas3251->regmap,
				   TAS3251_PLL_COEFF_0, tas3251->pll_p - 1);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL P: %d\n", ret);
			return ret;
		}

		ret = regmap_write(tas3251->regmap,
				   TAS3251_PLL_COEFF_1, tas3251->pll_j);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL J: %d\n", ret);
			return ret;
		}

		ret = regmap_write(tas3251->regmap,
				   TAS3251_PLL_COEFF_2, tas3251->pll_d >> 8);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL D msb: %d\n", ret);
			return ret;
		}

		ret = regmap_write(tas3251->regmap,
				   TAS3251_PLL_COEFF_3, tas3251->pll_d & 0xff);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL D lsb: %d\n", ret);
			return ret;
		}

		ret = regmap_write(tas3251->regmap,
				   TAS3251_PLL_COEFF_4, tas3251->pll_r - 1);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL R: %d\n", ret);
			return ret;
		}

		mck_rate = tas3251->real_pll;

		bclk_div = DIV_ROUND_CLOSEST(sck_rate, bclk_rate);
	}

	if (bclk_div > 128) {
		dev_err(dev, "Failed to find BCLK divider\n");
		return -EINVAL;
	}

	/* the actual rate */
	sample_rate = sck_rate / bclk_div / lrclk_div;
	osr_rate = 16 * sample_rate;

	/* run DSP no faster than 50 MHz */
	dsp_div = mck_rate > tas3251_dsp_max(tas3251) ? 2 : 1;

	dac_rate = tas3251_pllin_dac_rate(dai, osr_rate, pllin_rate);
	if (dac_rate) {
		/* the desired clock rate is "compatible" with the pll input
		 * clock, so use that clock as dac input instead of the pll
		 * output clock since the pll will introduce jitter and thus
		 * noise.
		 */
		dev_dbg(dev, "using pll input as dac input\n");
		ret = regmap_update_bits(tas3251->regmap, TAS3251_OSR_DAC_REF,		// reg(0)14
					 TAS3251_SDAC, TAS3251_SDAC_GPIO);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio as dacref: %d\n", ret);
			return ret;
		}

		gpio = TAS3251_GREF_SDOUT + tas3251->pll_in - 1;			// reg(0)18
		ret = regmap_update_bits(tas3251->regmap, TAS3251_GPIO_SDOUT,
					 TAS3251_GREF, gpio);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set SDOUT %d as dacin: %d\n",
				tas3251->pll_in, ret);
			return ret;
		}

		dacsrc_rate = pllin_rate;
	} else {
		/* run DAC no faster than 6144000 Hz */
		unsigned long dac_mul = tas3251_dac_max(tas3251, 6144000)
			/ osr_rate;
		unsigned long sck_mul = sck_rate / osr_rate;

		for (; dac_mul; dac_mul--) {
			if (!(sck_mul % dac_mul))
				break;
		}
		if (!dac_mul) {
			dev_err(dev, "Failed to find DAC rate\n");
			return -EINVAL;
		}

		dac_rate = dac_mul * osr_rate;
		dev_dbg(dev, "dac_rate %lu sample_rate %lu\n",
			dac_rate, sample_rate);

		ret = regmap_update_bits(tas3251->regmap, TAS3251_OSR_DAC_REF,		// reg(0)14
					 TAS3251_SDAC, TAS3251_SDAC_SCLK);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set sclk as dacref: %d\n", ret);
			return ret;
		}

		dacsrc_rate = sck_rate;
	}

	osr_div = DIV_ROUND_CLOSEST(dac_rate, osr_rate);
	if (osr_div > 128) {
		dev_err(dev, "Failed to find OSR divider\n");
		return -EINVAL;
	}

	dac_div = DIV_ROUND_CLOSEST(dacsrc_rate, dac_rate);
	if (dac_div > 128) {
		dev_err(dev, "Failed to find DAC divider\n");
		return -EINVAL;
	}
	dac_rate = dacsrc_rate / dac_div;

	ncp_div = DIV_ROUND_CLOSEST(dac_rate,
				    tas3251_ncp_target(tas3251, dac_rate));
	if (ncp_div > 128 || dac_rate / ncp_div > 2048000) {
		/* run NCP no faster than 2048000 Hz, but why? */
		ncp_div = DIV_ROUND_UP(dac_rate, 2048000);
		if (ncp_div > 128) {
			dev_err(dev, "Failed to find NCP divider\n");
			return -EINVAL;
		}
	}

	idac = mck_rate / (dsp_div * sample_rate);

	ret = regmap_write(tas3251->regmap, TAS3251_DSP_CLKDIV, dsp_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write DSP divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(tas3251->regmap, TAS3251_DAC_CLKDIV, dac_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write DAC divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(tas3251->regmap, TAS3251_NCP_CLKDIV, ncp_div - 1);			// reg 30
	if (ret != 0) {
		dev_err(dev, "Failed to write NCP divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(tas3251->regmap, TAS3251_OSR_CLKDIV, osr_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write OSR divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(tas3251->regmap,
			   TAS3251_MASTER_CLKDIV_1, bclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write BCLK divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(tas3251->regmap,
			   TAS3251_MASTER_CLKDIV_2, lrclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write LRCLK divider: %d\n", ret);
		return ret;
	}

//	ret = regmap_write(tas3251->regmap, TAS3251_IDAC_1, idac >> 8);				// reg 35, 36: DAC cycles per audio frame
//	if (ret != 0) {
//		dev_err(dev, "Failed to write IDAC msb divider: %d\n", ret);
//		return ret;
//	}

//	ret = regmap_write(tas3251->regmap, TAS3251_IDAC_2, idac & 0xff);
//	if (ret != 0) {
//		dev_err(dev, "Failed to write IDAC lsb divider: %d\n", ret);
//		return ret;
//	}

	if (sample_rate <= tas3251_dac_max(tas3251, 48000))
		fssp = TAS3251_FSSP_48KHZ;
	else
		fssp = TAS3251_FSSP_96KHZ;
	ret = regmap_update_bits(tas3251->regmap, TAS3251_FS_SPEED_MODE,
				 TAS3251_FSSP, fssp);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set fs speed: %d\n", ret);
		return ret;
	}

	dev_dbg(component->dev, "DSP divider %d\n", dsp_div);
	dev_dbg(component->dev, "DAC divider %d\n", dac_div);
	dev_dbg(component->dev, "NCP divider %d\n", ncp_div);
	dev_dbg(component->dev, "OSR divider %d\n", osr_div);
	dev_dbg(component->dev, "BCK divider %d\n", bclk_div);
	dev_dbg(component->dev, "LRCK divider %d\n", lrclk_div);
//	dev_dbg(component->dev, "IDAC %d\n", idac);
	dev_dbg(component->dev, "1<<FSSP %d\n", 1 << fssp);

	return 0;
}

static int tas3251_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	int alen;
	int gpio;
	int ret;

	dev_dbg(component->dev, "hw_params %u Hz, %u channels\n",
		params_rate(params),
		params_channels(params));

	switch (params_width(params)) {
	case 16:
		alen = TAS3251_ALEN_16;
		break;
	case 20:
		alen = TAS3251_ALEN_20;
		break;
	case 24:
		alen = TAS3251_ALEN_24;
		break;
	case 32:
		alen = TAS3251_ALEN_32;
		break;
	default:
		dev_err(component->dev, "Bad frame size: %d\n",
			params_width(params));
		return -EINVAL;
	}

	ret = regmap_update_bits(tas3251->regmap, TAS3251_I2S_1,				// reg 40
				 TAS3251_ALEN, alen);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set frame size: %d\n", ret);
		return ret;
	}

	if ((tas3251->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) ==
	    SND_SOC_DAIFMT_CBC_CFC) {
		ret = regmap_update_bits(tas3251->regmap, TAS3251_ERROR_DETECT,
					 TAS3251_DCAS, 0);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to enable clock divider autoset: %d\n",
				ret);
			return ret;
		}
		goto skip_pll;
	}


	if (tas3251->pll_out) {
		ret = regmap_write(tas3251->regmap, TAS3251_FLEX_A, 0x11);			// reg (253) 63, 64: Clock Flex
		if (ret != 0) {
			dev_err(component->dev, "Failed to set FLEX_A: %d\n", ret);
			return ret;
		}

		ret = regmap_write(tas3251->regmap, TAS3251_FLEX_B, 0xff);
		if (ret != 0) {
			dev_err(component->dev, "Failed to set FLEX_B: %d\n", ret);
			return ret;
		}

		ret = regmap_update_bits(tas3251->regmap, TAS3251_ERROR_DETECT,
					 TAS3251_IDFS | TAS3251_IDBK
					 | TAS3251_IDSK | TAS3251_IDCH
					 | TAS3251_IDCM | TAS3251_DCAS
					 | TAS3251_IPLK,
					 TAS3251_IDFS | TAS3251_IDBK
					 | TAS3251_IDSK | TAS3251_IDCH
					 | TAS3251_DCAS);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to ignore auto-clock failures: %d\n",
				ret);
			return ret;
		}
	} else {
		ret = regmap_update_bits(tas3251->regmap, TAS3251_ERROR_DETECT,
					 TAS3251_IDFS | TAS3251_IDBK
					 | TAS3251_IDSK | TAS3251_IDCH
					 | TAS3251_IDCM | TAS3251_DCAS
					 | TAS3251_IPLK,
					 TAS3251_IDFS | TAS3251_IDBK
					 | TAS3251_IDSK | TAS3251_IDCH
					 | TAS3251_DCAS | TAS3251_IPLK);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to ignore auto-clock failures: %d\n",
				ret);
			return ret;
		}

		ret = regmap_update_bits(tas3251->regmap, TAS3251_PLL_EN,
					 TAS3251_PLLE, 0);
		if (ret != 0) {
			dev_err(component->dev, "Failed to disable pll: %d\n", ret);
			return ret;
		}
	}

	ret = tas3251_set_dividers(dai, params);
	if (ret != 0)
		return ret;

	if (tas3251->pll_out) {
		ret = regmap_update_bits(tas3251->regmap, TAS3251_PLL_DSP_REF,			// reg 13
						TAS3251_SREF | TAS3251_SDSP,
						TAS3251_SREF_GPIO | TAS3251_SDSP_PLL);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio as pllref: %d\n", ret);
			return ret;
		}

		gpio = TAS3251_GREF_SDOUT;
		ret = regmap_update_bits(tas3251->regmap, TAS3251_GPIO_PLLIN,			// reg 18
					 TAS3251_GREF, gpio);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio %d as pllin: %d\n",
				tas3251->pll_in, ret);
			return ret;
		}

		ret = regmap_update_bits(tas3251->regmap, TAS3251_PLL_EN,			// reg 4
					 TAS3251_PLLE, TAS3251_PLLE);
		if (ret != 0) {
			dev_err(component->dev, "Failed to enable pll: %d\n", ret);
			return ret;
		}

		gpio = TAS3251_G2OE << (tas3251->pll_out - 1);
		ret = regmap_update_bits(tas3251->regmap, TAS3251_GPIO_EN,			// reg 8
					 gpio, gpio);
		if (ret != 0) {
			dev_err(component->dev, "Failed to enable gpio %d: %d\n",
				tas3251->pll_out, ret);
			return ret;
		}

		gpio = TAS3251_GPIO_SDOUT;							// reg 85
		ret = regmap_update_bits(tas3251->regmap, gpio,
					 TAS3251_G2SL, TAS3251_G2SL_PLLCK);			// 10 << 0
		if (ret != 0) {
			dev_err(component->dev, "Failed to output pll on %d: %d\n",
				ret, tas3251->pll_out);
			return ret;
		}
	}

skip_pll:
	return 0;
}

static int tas3251_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	int afmt;
	int offset = 0;
	int clock_output;
	int provider_mode;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		clock_output = 0;
		provider_mode = 0;
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		clock_output = TAS3251_SCLKO | TAS3251_LRKO;
		provider_mode = TAS3251_RLRK | TAS3251_RSCLK;
		break;
	case SND_SOC_DAIFMT_CBP_CFC:
		clock_output = TAS3251_SCLKO;
		provider_mode = TAS3251_RSCLK;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(tas3251->regmap, TAS3251_SCLK_LRCLK_CFG,		// reg 09
				 TAS3251_SCLKP | TAS3251_SCLKO | TAS3251_LRKO,
				 clock_output);
	if (ret != 0) {
		dev_err(component->dev, "Failed to enable clock output: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(tas3251->regmap, TAS3251_MASTER_MODE,			// reg 12
				 TAS3251_RLRK | TAS3251_RSCLK,
				 provider_mode);
	if (ret != 0) {
		dev_err(component->dev, "Failed to enable provider mode: %d\n", ret);
		return ret;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		afmt = TAS3251_AFMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		afmt = TAS3251_AFMT_RTJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		afmt = TAS3251_AFMT_LTJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		offset = 1;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_B:
		afmt = TAS3251_AFMT_DSP;
		break;
	default:
		dev_err(component->dev, "unsupported DAI format: 0x%x\n",
			tas3251->fmt);
		return -EINVAL;
	}

	ret = regmap_update_bits(tas3251->regmap, TAS3251_I2S_1,			// reg 40
				 TAS3251_AFMT, afmt);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data format: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(tas3251->regmap, TAS3251_I2S_2,			// reg 41
				 0xFF, offset);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data offset: %d\n", ret);
		return ret;
	}

	tas3251->fmt = fmt;

	return 0;
}

static int tas3251_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);

	if (ratio > 256)
		return -EINVAL;

	tas3251->bclk_ratio = ratio;

	return 0;
}

static int tas3251_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas3251_priv *tas3251 = snd_soc_component_get_drvdata(component);
	int ret;
	unsigned int mute_det;

	mutex_lock(&tas3251->mutex);

	if (mute) {
		tas3251->mute |= 0x1;
		ret = regmap_update_bits(tas3251->regmap, TAS3251_MUTE,			// reg 3
					 TAS3251_RQML | TAS3251_RQMR,
					 TAS3251_RQML | TAS3251_RQMR);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set digital mute: %d\n", ret);
			goto unlock;
		}

		regmap_read_poll_timeout(tas3251->regmap,
					 TAS3251_ANALOG_MUTE_DET,			// reg 108
					 mute_det, (mute_det & 0x3) == 0,
					 200, 10000);
	} else {
		tas3251->mute &= ~0x1;
		ret = tas3251_update_mute(tas3251);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to update digital mute: %d\n", ret);
			goto unlock;
		}

		regmap_read_poll_timeout(tas3251->regmap,
					 TAS3251_ANALOG_MUTE_DET,			// reg 108
					 mute_det,
					 (mute_det & 0x3)
					 == ((~tas3251->mute >> 1) & 0x3),
					 200, 10000);
	}

unlock:
	mutex_unlock(&tas3251->mutex);

	return ret;
}

static const struct snd_soc_dai_ops tas3251_dai_ops = {
	.startup = tas3251_dai_startup,
	.hw_params = tas3251_hw_params,
	.set_fmt = tas3251_set_fmt,
	.mute_stream = tas3251_mute,
	.set_bclk_ratio = tas3251_set_bclk_ratio,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver tas3251_dai = {
	.name = "tas3251-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 32000,
		.rate_max = 96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE
	},
	.ops = &tas3251_dai_ops,
};

static const struct snd_soc_component_driver tas3251_component_driver = {
	.set_bias_level		= tas3251_set_bias_level,
	.controls		= tas3251_controls,
	.num_controls		= ARRAY_SIZE(tas3251_controls),
	.dapm_widgets		= tas3251_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas3251_dapm_widgets),
	.dapm_routes		= tas3251_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(tas3251_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_range_cfg tas3251_range = {
	.name = "Pages", .range_min = TAS3251_VIRT_BASE,
	.range_max = TAS3251_MAX_REGISTER,
	.range_max = 257,
	.selector_reg = TAS3251_PAGE,
	.selector_mask = 0xff,
	.window_start = 0, .window_len = 0x100,
};

const struct regmap_config tas3251_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = tas3251_readable,
	.volatile_reg = tas3251_volatile,

	.ranges = &tas3251_range,
	.num_ranges = 1,

	.max_register = TAS3251_MAX_REGISTER,
	.reg_defaults = tas3251_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tas3251_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(tas3251_regmap);

int tas3251_probe(struct device *dev, struct regmap *regmap)
{
	struct tas3251_priv *tas3251;
	int i, ret;

	tas3251 = devm_kzalloc(dev, sizeof(struct tas3251_priv), GFP_KERNEL);
	if (!tas3251)
		return -ENOMEM;

	mutex_init(&tas3251->mutex);

	dev_set_drvdata(dev, tas3251);
	tas3251->regmap = regmap;

	for (i = 0; i < ARRAY_SIZE(tas3251->supplies); i++)
		tas3251->supplies[i].supply = tas3251_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(tas3251->supplies),
				      tas3251->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	tas3251->supply_nb[0].notifier_call = tas3251_regulator_event_0;
	tas3251->supply_nb[1].notifier_call = tas3251_regulator_event_1;
	tas3251->supply_nb[2].notifier_call = tas3251_regulator_event_2;
	tas3251->supply_nb[2].notifier_call = tas3251_regulator_event_3;
	tas3251->supply_nb[2].notifier_call = tas3251_regulator_event_4;

	for (i = 0; i < ARRAY_SIZE(tas3251->supplies); i++) {
		ret = devm_regulator_register_notifier(
						tas3251->supplies[i].consumer,
						&tas3251->supply_nb[i]);
		if (ret != 0) {
			dev_err(dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(tas3251->supplies),
				    tas3251->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Reset the device, verifying I/O in the process for I2C */
	ret = regmap_write(regmap, TAS3251_RESET,
			   TAS3251_RSTM | TAS3251_RSTR);
	if (ret != 0) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	ret = regmap_write(regmap, TAS3251_RESET, 0);
	if (ret != 0) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	tas3251->sclk = devm_clk_get(dev, NULL);
	if (PTR_ERR(tas3251->sclk) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err;
	}
	if (!IS_ERR(tas3251->sclk)) {
		ret = clk_prepare_enable(tas3251->sclk);
		if (ret != 0) {
			dev_err(dev, "Failed to enable SCLK: %d\n", ret);
			goto err;
		}
	}

	/* Default to standby mode */
	ret = regmap_update_bits(tas3251->regmap, TAS3251_POWER,
				 TAS3251_RQST, TAS3251_RQST);
	if (ret != 0) {
		dev_err(dev, "Failed to request standby: %d\n",
			ret);
		goto err_clk;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

#ifdef CONFIG_OF
	if (dev->of_node) {
		const struct device_node *np = dev->of_node;
		u32 val;

		if (of_property_read_u32(np, "pll-in", &val) >= 0) {
			if (val > 6) {
				dev_err(dev, "Invalid pll-in\n");
				ret = -EINVAL;
				goto err_clk;
			}
			tas3251->pll_in = val;
		}

		if (of_property_read_u32(np, "pll-out", &val) >= 0) {
			if (val > 6) {
				dev_err(dev, "Invalid pll-out\n");
				ret = -EINVAL;
				goto err_clk;
			}
			tas3251->pll_out = val;
		}

		if (!tas3251->pll_in != !tas3251->pll_out) {
			dev_err(dev,
				"Error: both pll-in and pll-out, or none\n");
			ret = -EINVAL;
			goto err_clk;
		}
		if (tas3251->pll_in && tas3251->pll_in == tas3251->pll_out) {
			dev_err(dev, "Error: pll-in == pll-out\n");
			ret = -EINVAL;
			goto err_clk;
		}
	}
#endif

	ret = devm_snd_soc_register_component(dev, &tas3251_component_driver,
				    &tas3251_dai, 1);
	if (ret != 0) {
		dev_err(dev, "Failed to register CODEC: %d\n", ret);
		goto err_pm;
	}

	return 0;

err_pm:
	pm_runtime_disable(dev);
err_clk:
	if (!IS_ERR(tas3251->sclk))
		clk_disable_unprepare(tas3251->sclk);
err:
	regulator_bulk_disable(ARRAY_SIZE(tas3251->supplies),
				     tas3251->supplies);
	return ret;
}
EXPORT_SYMBOL_GPL(tas3251_probe);

void tas3251_remove(struct device *dev)
{
	struct tas3251_priv *tas3251 = dev_get_drvdata(dev);

	pm_runtime_disable(dev);
	if (!IS_ERR(tas3251->sclk))
		clk_disable_unprepare(tas3251->sclk);
	regulator_bulk_disable(ARRAY_SIZE(tas3251->supplies),
			       tas3251->supplies);
}
EXPORT_SYMBOL_GPL(tas3251_remove);

#ifdef CONFIG_PM
static int tas3251_suspend(struct device *dev)
{
	struct tas3251_priv *tas3251 = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(tas3251->regmap, TAS3251_POWER,
					TAS3251_RQPD | TAS3251_DSPR,
					TAS3251_RQPD | TAS3251_DSPR);
	if (ret != 0) {
		dev_err(dev, "Failed to request power down: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(tas3251->supplies),
				     tas3251->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to disable supplies: %d\n", ret);
		return ret;
	}

	if (!IS_ERR(tas3251->sclk))
		clk_disable_unprepare(tas3251->sclk);

	return 0;
}

static int tas3251_resume(struct device *dev)
{
	struct tas3251_priv *tas3251 = dev_get_drvdata(dev);
	int ret;

	if (!IS_ERR(tas3251->sclk)) {
		ret = clk_prepare_enable(tas3251->sclk);
		if (ret != 0) {
			dev_err(dev, "Failed to enable SCLK: %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(tas3251->supplies),
				    tas3251->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	regcache_cache_only(tas3251->regmap, false);
	ret = regcache_sync(tas3251->regmap);
	if (ret != 0) {
		dev_err(dev, "Failed to sync cache: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(tas3251->regmap, TAS3251_POWER,
				 TAS3251_RQPD | TAS3251_DSPR, 0);
	if (ret != 0) {
		dev_err(dev, "Failed to remove power down: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

const struct dev_pm_ops tas3251_pm_ops = {
	SET_RUNTIME_PM_OPS(tas3251_suspend, tas3251_resume, NULL)
};
EXPORT_SYMBOL_GPL(tas3251_pm_ops);

MODULE_DESCRIPTION("ASoC TAS3251 codec driver");
MODULE_AUTHOR("JPv Coolwijk <jpvc36@gmail.com>");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_LICENSE("GPL v2");
