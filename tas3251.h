/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for the TAS3251 CODECs
 *
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 */

#ifndef _SND_SOC_TAS3251
#define _SND_SOC_TAS3251

#include <linux/pm.h>
#include <linux/regmap.h>

#define TAS3251_VIRT_BASE 0x100
#define TAS3251_PAGE_LEN  0x100
#define TAS3251_PAGE_BASE(n)  (TAS3251_VIRT_BASE + (TAS3251_PAGE_LEN * n))

#define TAS3251_PAGE              0

#define TAS3251_RESET             (TAS3251_PAGE_BASE(0) +   1)
#define TAS3251_POWER             (TAS3251_PAGE_BASE(0) +   2)
#define TAS3251_MUTE              (TAS3251_PAGE_BASE(0) +   3)
#define TAS3251_PLL_EN            (TAS3251_PAGE_BASE(0) +   4)
#define TAS3251_I2C_PAGE_AUTO_INC (TAS3251_PAGE_BASE(0) +   6)
#define TAS3251_DSP               (TAS3251_PAGE_BASE(0) +   7)
#define TAS3251_GPIO_EN           (TAS3251_PAGE_BASE(0) +   8)
#define TAS3251_SCLK_LRCLK_CFG    (TAS3251_PAGE_BASE(0) +   9)
#define TAS3251_MASTER_MODE       (TAS3251_PAGE_BASE(0) +  12)
#define TAS3251_PLL_DSP_REF       (TAS3251_PAGE_BASE(0) +  13)
#define TAS3251_OSR_DAC_REF       (TAS3251_PAGE_BASE(0) +  14)
#define TAS3251_NCP_REF           (TAS3251_PAGE_BASE(0) +  15)
#define TAS3251_GPIO_DACIN        (TAS3251_PAGE_BASE(0) +  16)
#define TAS3251_GPIO_NCPIN        (TAS3251_PAGE_BASE(0) +  17)
#define TAS3251_GPIO_PLLIN        (TAS3251_PAGE_BASE(0) +  18)
#define TAS3251_PLL_COEFF_0       (TAS3251_PAGE_BASE(0) +  20)
#define TAS3251_PLL_COEFF_1       (TAS3251_PAGE_BASE(0) +  21)
#define TAS3251_PLL_COEFF_2       (TAS3251_PAGE_BASE(0) +  22)
#define TAS3251_PLL_COEFF_3       (TAS3251_PAGE_BASE(0) +  23)
#define TAS3251_PLL_COEFF_4       (TAS3251_PAGE_BASE(0) +  24)
#define TAS3251_DSP_CLKDIV        (TAS3251_PAGE_BASE(0) +  27)
#define TAS3251_DAC_CLKDIV        (TAS3251_PAGE_BASE(0) +  28)
#define TAS3251_NCP_CLKDIV        (TAS3251_PAGE_BASE(0) +  29)
#define TAS3251_OSR_CLKDIV        (TAS3251_PAGE_BASE(0) +  30)
#define TAS3251_MASTER_CLKDIV_1   (TAS3251_PAGE_BASE(0) +  32)
#define TAS3251_MASTER_CLKDIV_2   (TAS3251_PAGE_BASE(0) +  33)
#define TAS3251_FS_SPEED_MODE     (TAS3251_PAGE_BASE(0) +  34)
#define TAS3251_ERROR_DETECT      (TAS3251_PAGE_BASE(0) +  37)
#define TAS3251_I2S_1             (TAS3251_PAGE_BASE(0) +  40)
#define TAS3251_I2S_2             (TAS3251_PAGE_BASE(0) +  41)
#define TAS3251_DAC_ROUTING       (TAS3251_PAGE_BASE(0) +  42)
#define TAS3251_DSP_PROGRAM       (TAS3251_PAGE_BASE(0) +  43)
#define TAS3251_CLKDET            (TAS3251_PAGE_BASE(0) +  44)
#define TAS3251_AUTO_MUTE         (TAS3251_PAGE_BASE(0) +  59)
#define TAS3251_DIGITAL_VOLUME_1  (TAS3251_PAGE_BASE(0) +  60)
#define TAS3251_DIGITAL_VOLUME_2  (TAS3251_PAGE_BASE(0) +  61)
#define TAS3251_DIGITAL_VOLUME_3  (TAS3251_PAGE_BASE(0) +  62)
#define TAS3251_DIGITAL_MUTE_1    (TAS3251_PAGE_BASE(0) +  63)
#define TAS3251_DIGITAL_MUTE_2    (TAS3251_PAGE_BASE(0) +  64)
#define TAS3251_DIGITAL_MUTE_3    (TAS3251_PAGE_BASE(0) +  65)
#define TAS3251_DITHER            (TAS3251_PAGE_BASE(0) +  67)
#define TAS3251_DITHER_1          (TAS3251_PAGE_BASE(0) +  68)
#define TAS3251_DITHER_2          (TAS3251_PAGE_BASE(0) +  69)
#define TAS3251_DITHER_3          (TAS3251_PAGE_BASE(0) +  70)
#define TAS3251_DITHER_4          (TAS3251_PAGE_BASE(0) +  71)
#define TAS3251_DITHER_GAIN       (TAS3251_PAGE_BASE(0) +  72)
#define TAS3251_DITHER_5          (TAS3251_PAGE_BASE(0) +  73)
#define TAS3251_DITHER_6          (TAS3251_PAGE_BASE(0) +  74)
#define TAS3251_DITHER_7          (TAS3251_PAGE_BASE(0) +  75)
#define TAS3251_DITHER_8          (TAS3251_PAGE_BASE(0) +  76)
#define TAS3251_DACL_OFFSET       (TAS3251_PAGE_BASE(0) +  78)
#define TAS3251_DACR_OFFSET       (TAS3251_PAGE_BASE(0) +  79)
#define TAS3251_GPIO_SDOUT        (TAS3251_PAGE_BASE(0) +  85)
#define TAS3251_GPIO_CONTROL_1    (TAS3251_PAGE_BASE(0) +  86)
#define TAS3251_GPIO_CONTROL_2    (TAS3251_PAGE_BASE(0) +  87)
#define TAS3251_DIEI              (TAS3251_PAGE_BASE(0) +  88)
#define TAS3251_RATE_DET_1        (TAS3251_PAGE_BASE(0) +  91)
#define TAS3251_RATE_DET_2        (TAS3251_PAGE_BASE(0) +  92)
#define TAS3251_RATE_DET_3        (TAS3251_PAGE_BASE(0) +  93)
#define TAS3251_RATE_DET_4        (TAS3251_PAGE_BASE(0) +  94)
#define TAS3251_CLOCK_STATUS      (TAS3251_PAGE_BASE(0) +  95)
#define TAS3251_ANALOG_MUTE_DET   (TAS3251_PAGE_BASE(0) + 108)
#define TAS3251_GPIN              (TAS3251_PAGE_BASE(0) + 119)
#define TAS3251_DIGITAL_MUTE_DET  (TAS3251_PAGE_BASE(0) + 120)

#define TAS3251_OUTPUT_AMPLITUDE  (TAS3251_PAGE_BASE(1) +   1)
#define TAS3251_ANALOG_GAIN_CTRL  (TAS3251_PAGE_BASE(1) +   2)
#define TAS3251_ANALOG_MUTE_CTRL  (TAS3251_PAGE_BASE(1) +   6)
#define TAS3251_ANALOG_GAIN_BOOST (TAS3251_PAGE_BASE(1) +   7)
#define TAS3251_VCOM_CTRL_2       (TAS3251_PAGE_BASE(1) +   9)

#define TAS3251_FLEX_A            (TAS3251_PAGE_BASE(253) + 63)
#define TAS3251_FLEX_B            (TAS3251_PAGE_BASE(253) + 64)

#define TAS3251_MAX_REGISTER      (TAS3251_PAGE_BASE(253) + 64)

/* Page 0, Register 1 - reset */
#define TAS3251_RSTR		(1 << 0)
#define TAS3251_RSTM		(1 << 4)

/* Page 0, Register 2 - power */
#define TAS3251_RQPD		(1 << 0)
#define TAS3251_RQPD_SHIFT	0
#define TAS3251_RQST		(1 << 4)
#define TAS3251_RQST_SHIFT	4
#define TAS3251_DSPR		(1 << 7)
#define TAS3251_DSPR_SHIFT	7

/* Page 0, Register 3 - mute */
#define TAS3251_RQMR		(1 << 0)
#define TAS3251_RQMR_SHIFT	0
#define TAS3251_RQML		(1 << 4)
#define TAS3251_RQML_SHIFT	4

/* Page 0, Register 4 - PLL */
#define TAS3251_PLLE		(1 << 0)
#define TAS3251_PLLE_SHIFT	0
#define TAS3251_PLCK		(1 << 4)
#define TAS3251_PLCK_SHIFT	4

/* Page 0, Register 7 - DSP */
#define TAS3251_SDSL		(1 << 0)
#define TAS3251_SDSL_SHIFT	0
#define TAS3251_DEMP		(1 << 4)
#define TAS3251_DEMP_SHIFT	4

/* Page 0, Register 8 - GPIO output enable */
#define TAS3251_MUTEOE		(1 << 4)
#define TAS3251_G2OE		(1 << 5)

/* Page 0, Register 9 - BCK, LRCLK configuration */
#define TAS3251_LRKO		(1 << 0)
#define TAS3251_LRKO_SHIFT	0
#define TAS3251_SCLKO		(1 << 4)
#define TAS3251_SCLKO_SHIFT	4
#define TAS3251_SCLKP		(1 << 5)
#define TAS3251_SCLKP_SHIFT	5

/* Page 0, Register 12 - Master mode SCLK, LRCLK reset */
#define TAS3251_RLRK		(1 << 0)
#define TAS3251_RLRK_SHIFT	0
#define TAS3251_RSCLK		(1 << 1)
#define TAS3251_RSCLK_SHIFT	1

/* Page 0, Register 13 - PLL, DSP reference */
#define TAS3251_SREF		(7 << 4)
#define TAS3251_SREF_SHIFT	4
#define TAS3251_SREF_MCLK	(0 << 4)
#define TAS3251_SREF_SCLK	(1 << 4)
#define TAS3251_SREF_OSC	(2 << 4)
#define TAS3251_SREF_GPIO	(3 << 4)

#define TAS3251_SDSP		(7 << 0)
#define TAS3251_SDSP_SHIFT	0
#define TAS3251_SDSP_MCK	(0 << 0)
#define TAS3251_SDSP_PLL	(1 << 0)
#define TAS3251_SDSP_OSC	(2 << 0)
#define TAS3251_SDSP_MCLK	(3 << 0)
#define TAS3251_SDSP_SCLK	(4 << 0)
#define TAS3251_SDSP_GPIO	(5 << 0)

/* Page 0, Register 14 - DAC, OSR reference */
#define TAS3251_SDAC		(7 << 4)
#define TAS3251_SDAC_SHIFT	4
#define TAS3251_SDAC_MCK	(0 << 4)
#define TAS3251_SDAC_PLL	(1 << 4)
#define TAS3251_SDAC_MCLK	(3 << 4)
#define TAS3251_SDAC_SCLK	(4 << 4)
#define TAS3251_SDAC_GPIO	(5 << 4)

#define TAS3251_SOSR		(7 << 0)
#define TAS3251_SOSR_SHIFT	0
#define TAS3251_SOSR_DAC	(0 << 0)
#define TAS3251_SOSR_MCK	(1 << 0)
#define TAS3251_SOSR_PLL	(2 << 0)
#define TAS3251_SOSR_OSC	(3 << 0)
#define TAS3251_SOSR_MCLK	(4 << 0)
#define TAS3251_SOSR_SCLK	(5 << 0)
#define TAS3251_SOSR_GPIO	(6 << 0)

/* Page 0, Register 15 - GPIO source for NCP */
#define TAS3251_SNCP		(7 << 0)
#define TAS3251_SNCP_SHIFT	0
#define TAS3251_SNCP_DAC	(0 << 0)
#define TAS3251_SNCP_MCK	(1 << 0)
#define TAS3251_SNCP_PLL	(2 << 0)
#define TAS3251_SNCP_OSC	(3 << 0)
#define TAS3251_SNCP_MCLK	(4 << 0)
#define TAS3251_SNCP_SCLK	(5 << 0)
#define TAS3251_SNCP_GPIO	(6 << 0)

/* Page 0, Register 16 - GPIO source for DAC, DSP */
#define TAS3251_GDAC		(7 << 0)
#define TAS3251_GDAC_SHIFT	0
#define TAS3251_GDAC_SDOUT	(5 << 0)

#define TAS3251_GDSP		(7 << 4)
#define TAS3251_GDSP_SHIFT	4
#define TAS3251_GDSP_SDOUT	(5 << 4)

/* Page 0, Register 17 - GPIO source for NCP, OSR */
#define TAS3251_GOSR		(7 << 0)
#define TAS3251_GOSR_SHIFT	0
#define TAS3251_GOSR_SDOUT	(5 << 0)

#define TAS3251_GNCP		(7 << 4)
#define TAS3251_GNCP_SHIFT	4
#define TAS3251_GNCP_SDOUT	(5 << 4)

/* Page 0, Register 18 - GPIO source for PLL */
#define TAS3251_GREF		(7 << 0)
#define TAS3251_GREF_SHIFT	0
#define TAS3251_GREF_SDOUT	(5 << 0)

/* Page 0, Register 34 - fs speed mode, interpolation */
#define TAS3251_FSSP		(3 << 0)
#define TAS3251_FSSP_SHIFT	0
#define TAS3251_FSSP_48KHZ	(3 << 0)
#define TAS3251_FSSP_96KHZ	(4 << 0)
#define TAS3251_FSSP_32KHZ	(7 << 0)

#define TAS3251_I16E		(1 << 4)
#define TAS3251_I16E_SHIFT	4

/* Page 0, Register 37 - Error detection */
#define TAS3251_IPLK		(1 << 0)
#define TAS3251_DCAS		(1 << 1)
#define TAS3251_IDCM		(1 << 2)
#define TAS3251_IDCH		(1 << 3)
#define TAS3251_IDSK		(1 << 4)
#define TAS3251_IDBK		(1 << 5)
#define TAS3251_IDFS		(1 << 6)

/* Page 0, Register 40 - I2S configuration */
#define TAS3251_ALEN		(3 << 0)
#define TAS3251_ALEN_SHIFT	0
#define TAS3251_ALEN_16		(0 << 0)
#define TAS3251_ALEN_20		(1 << 0)
#define TAS3251_ALEN_24		(2 << 0)
#define TAS3251_ALEN_32		(3 << 0)

#define TAS3251_AFMT		(3 << 4)
#define TAS3251_AFMT_SHIFT	4
#define TAS3251_AFMT_I2S	(0 << 4)
#define TAS3251_AFMT_DSP	(1 << 4)
#define TAS3251_AFMT_RTJ	(2 << 4)
#define TAS3251_AFMT_LTJ	(3 << 4)

/* Page 0, Register 42 - DAC routing */
#define TAS3251_AUPR_SHIFT	0
#define TAS3251_AUPL_SHIFT	4

/* Page 0, Register 59 - auto mute */
#define TAS3251_ATMR_SHIFT	0
#define TAS3251_ATML_SHIFT	4

/* Page 0, Register 63 - ramp rates */
#define TAS3251_VNDF_SHIFT	6
#define TAS3251_VNDS_SHIFT	4
#define TAS3251_VNUF_SHIFT	2
#define TAS3251_VNUS_SHIFT	0

/* Page 0, Register 64 - emergency ramp rates */
#define TAS3251_VEDF_SHIFT	6
#define TAS3251_VEDS_SHIFT	4

/* Page 0, Register 65 - Digital mute enables */
#define TAS3251_ACTL_SHIFT	2
#define TAS3251_AMLE_SHIFT	1
#define TAS3251_AMRE_SHIFT	0

/* Page 0, Register 67 - Dither */
#define TAS3251_DLPA_SHIFT	6
#define TAS3251_DRPA_SHIFT	4
#define TAS3251_DLPM_SHIFT	2
#define TAS3251_DRPM_SHIFT	0

/* Page 0, Register 72 - Dither gain*/
#define TAS3251_DLSA_SHIFT	6
#define TAS3251_DRSA_SHIFT	4
#define TAS3251_DLSM_SHIFT	2
#define TAS3251_DRSM_SHIFT	0

/* Page 0, Register 85, GPIO output selection */
#define TAS3251_G2SL		(31 << 0)
#define TAS3251_G2SL_SHIFT	0
#define TAS3251_G2SL_OFF	(0 << 0)
#define TAS3251_G2SL_DSP	(1 << 0)
#define TAS3251_G2SL_REG	(2 << 0)
#define TAS3251_GxSL_AMUTB	(3 << 0)
#define TAS3251_G2SL_AMUTL	(4 << 0)
#define TAS3251_G2SL_AMUTR	(5 << 0)
#define TAS3251_G2SL_CLKI	(6 << 0)
#define TAS3251_G2SL_SDOUT	(7 << 0)
#define TAS3251_G2SL_ANMUL	(8 << 0)
#define TAS3251_GxSL_ANMUR	(9 << 0)
#define TAS3251_G2SL_PLLLK	(10 << 0)
#define TAS3251_G2SL_CPCLK	(11 << 0)
#define TAS3251_G2SL_SHORTL	(12 << 0)
#define TAS3251_G2SL_SHORTR	(13 << 0)
#define TAS3251_G2SL_UV0_7	(14 << 0)
#define TAS3251_G2SL_UV0_3	(15 << 0)
#define TAS3251_G2SL_PLLCK	(16 << 0)
#define TAS3251_G2SL_OSCCK	(17 << 0)
#define TAS3251_G2SL_IMPL	(18 << 0)
#define TAS3251_G2SL_IMPR	(19 << 0)
#define TAS3251_G2SL_UVP	(20 << 0)
#define TAS3251_G2SL_OFFS	(21 << 0)
#define TAS3251_G2SL_CLKERR	(22 << 0)
#define TAS3251_G2SL_CLKCHG	(23 << 0)
#define TAS3251_G2SL_CLKMISS	(24 << 0)
#define TAS3251_G2SL_CLKHALT	(25 << 0)
#define TAS3251_G2SL_DSP_BOOT	(26 << 0)
#define TAS3251_G2SL_CP_VALID	(27 << 0)

/* Page 1, Register 2 - analog volume control */
#define TAS3251_RAGN_SHIFT	0
#define TAS3251_LAGN_SHIFT	4

/* Page 1, Register 7 - analog boost control */
#define TAS3251_AGBR_SHIFT	0
#define TAS3251_AGBL_SHIFT	4

extern const struct dev_pm_ops tas3251_pm_ops;
extern const struct regmap_config tas3251_regmap;

int tas3251_probe(struct device *dev, struct regmap *regmap);
void tas3251_remove(struct device *dev);

#endif

