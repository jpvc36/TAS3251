// SPDX-License-Identifier: GPL-2.0
/*
 * Clock Driver for HiFiBerry DAC+ HD
 *
 * Author: Joerg Schambacher, i2Audio GmbH for HiFiBerry
 *         Copyright 2020
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

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#define PLL_RESET			1
#define DEFAULT_RATE			44100

static struct reg_default common_pll_regs[] = {
	{0x02, 0x53}, {0x03, 0xFE}, {0x07, 0x00}, {0x0F, 0x00},		// 2x MASKS, CLKx_OEB, I2C_REG, PLL
	{0x10, 0x0D}, {0x11, 0x8C}, {0x12, 0x8C}, {0x13, 0x8C},		// 4x MSx CLKx
	{0x14, 0x8C}, {0x15, 0x8C}, {0x16, 0x8C}, {0x17, 0x8C},		// 4x MSx CLKx
	{0x18, 0x2A}, {0x1C, 0x00}, {0x1D, 0x0F}, {0x1F, 0x00},		// CLKx DIS_STATE, MSNA
	{0x2A, 0x00}, {0x2C, 0x00}, {0x2F, 0x00}, {0x30, 0x00},		// MS0, R0_DIV, MS0
	{0x31, 0x00}, {0xB7, 0x92},					// MS0, XTAL_CL
	{0xB1, 0xAC},							// PLLx_RST
	};
static struct reg_default dedicated_44k1_pll_regs[] = {
	{0x1A, 0x3D}, {0x1B, 0x09}, {0x1E, 0xD6}, {0x20, 0x19},		// MSNA
	{0x21, 0x7A}, {0x2B, 0x04}, {0x2D, 0x07}, {0x2E, 0xE0},		// MSNA, MS0, R0_DIV
	{0xB1, 0xAC},							// PLLx_RST
	};
static struct reg_default dedicated_48k_pll_regs[] = {
	{0x1A, 0x0C}, {0x1B, 0x35}, {0x1E, 0xF0}, {0x20, 0x09},		// MSNA
	{0x21, 0x50}, {0x2B, 0x04}, {0x2D, 0x07}, {0x2E, 0x20},		// MSNA, MS0, R0_DIV
	{0xB1, 0xAC},							// PLLx_RST
	};

/*
 * struct clk_hifiberry_drvdata - Common struct to the HiFiBerry DAC HD Clk
 * @hw: clk_hw for the common clk framework
 */
struct clk_hifiberry_drvdata {
	struct regmap *regmap;
	struct clk *clk;
	struct clk_hw hw;
	unsigned long rate;
};

#define to_hifiberry_clk(_hw) \
	container_of(_hw, struct clk_hifiberry_drvdata, hw)

static int clk_hifiberry_dachd_write_pll_regs(struct regmap *regmap,
				struct reg_default *regs,			// {unsigned int reg; unsigned int def;};
				int num)
{
	int i;
	int ret = 0;

	for (i = 0; i < num; i++) {
		ret |= regmap_write(regmap, regs[i].reg, regs[i].def);
	}
		mdelay(10);
	return ret;
}

static unsigned long clk_hifiberry_dachd_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	return to_hifiberry_clk(hw)->rate;
}

static long clk_hifiberry_dachd_round_rate(struct clk_hw *hw,
	unsigned long rate, unsigned long *parent_rate)
{
	return rate;
}

static int clk_hifiberry_dachd_set_rate(struct clk_hw *hw,
	unsigned long rate, unsigned long parent_rate)
{
	int ret;
	struct clk_hifiberry_drvdata *drvdata = to_hifiberry_clk(hw);

	switch (rate) {
	case 44100:
	case 88200:
	case 176400:
		ret = clk_hifiberry_dachd_write_pll_regs(drvdata->regmap,
			dedicated_44k1_pll_regs, ARRAY_SIZE(dedicated_44k1_pll_regs));
		break;
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		ret = clk_hifiberry_dachd_write_pll_regs(drvdata->regmap,
			dedicated_48k_pll_regs, ARRAY_SIZE(dedicated_48k_pll_regs));
		break;
	default:
		ret = -EINVAL;
		break;
	}
	to_hifiberry_clk(hw)->rate = rate;

	return ret;
}

const struct clk_ops clk_hifiberry_dachd_rate_ops = {
	.recalc_rate = clk_hifiberry_dachd_recalc_rate,
	.round_rate = clk_hifiberry_dachd_round_rate,
	.set_rate = clk_hifiberry_dachd_set_rate,
};

static int clk_hifiberry_dachd_remove(struct device *dev)
{
	of_clk_del_provider(dev->of_node);
	return 0;
}

const struct regmap_config hifiberry_pll_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};
EXPORT_SYMBOL_GPL(hifiberry_pll_regmap);


static int clk_hifiberry_dachd_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct clk_hifiberry_drvdata *hdclk;
	int ret = 0;
	u32 i2c_reg, clkout = 0;
	struct clk_init_data init;
	struct device *dev = &i2c->dev;
	struct device_node *dev_node = dev->of_node;
	struct regmap_config config = hifiberry_pll_regmap;

	hdclk = devm_kzalloc(&i2c->dev,
			sizeof(struct clk_hifiberry_drvdata), GFP_KERNEL);
	if (!hdclk)
		return -ENOMEM;

	i2c_set_clientdata(i2c, hdclk);

	hdclk->regmap = devm_regmap_init_i2c(i2c, &config);

	if (IS_ERR(hdclk->regmap))
		return PTR_ERR(hdclk->regmap);

	of_property_read_u32(dev_node, "reg", &i2c_reg);			// Read i2c-reg
	if ((i2c_reg >= 0x60) & (i2c_reg <= 0x6f)) {
		dev_dbg(dev, "I2C-reg = 0x%x", i2c_reg);
		common_pll_regs[2].def = (i2c_reg - 0x60) << 4;			// Set i2c-reg in 0x07
	}

	if (!of_property_read_u32(dev_node, "clkout", &clkout)) {		// Read clkout
			if (clkout == 1) {
				common_pll_regs[1].def = (0xFF ^ (1 << 1));
				common_pll_regs[4].def = 0x8C;
				common_pll_regs[5].def = 0x0D;
			}
			else if (clkout == 2) {
				common_pll_regs[1].def = (0xFF ^ (1 << 2));
				common_pll_regs[4].def = 0x8C;
				common_pll_regs[6].def = 0x0D;
			}
		dev_dbg(dev, "MCLK Output: OUT%d", clkout);
	}

	/* restart PLL */
	ret = clk_hifiberry_dachd_write_pll_regs(hdclk->regmap, common_pll_regs,
					ARRAY_SIZE(common_pll_regs));
//	dev_dbg(dev, "Size common_pll_regs = %lu", ARRAY_SIZE(common_pll_regs));
	if (ret)
		return ret;

	init.name = "clk-hifiberry-dachd";
	init.ops = &clk_hifiberry_dachd_rate_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	hdclk->hw.init = &init;

	hdclk->clk = devm_clk_register(dev, &hdclk->hw);
	if (IS_ERR(hdclk->clk)) {
		dev_err(dev, "unable to register %s\n",	init.name);
		return PTR_ERR(hdclk->clk);
	}

	ret = of_clk_add_provider(dev_node, of_clk_src_simple_get, hdclk->clk);
	if (ret != 0) {
		dev_err(dev, "Cannot of_clk_add_provider");
		return ret;
	}

	ret = clk_set_rate(hdclk->hw.clk, DEFAULT_RATE);
	if (ret != 0) {
		dev_err(dev, "Cannot set rate : %d\n",	ret);
		return -EINVAL;
	}
	return ret;
}

static void clk_hifiberry_dachd_i2c_remove(struct i2c_client *i2c)
{
	clk_hifiberry_dachd_remove(&i2c->dev);
}

static const struct i2c_device_id clk_hifiberry_dachd_i2c_id[] = {
	{ "dachd-clk", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, clk_hifiberry_dachd_i2c_id);

static const struct of_device_id clk_hifiberry_dachd_of_match[] = {
	{ .compatible = "ti,tas3251hd-clk", },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_hifiberry_dachd_of_match);

static struct i2c_driver clk_hifiberry_dachd_i2c_driver = {
	.probe		= clk_hifiberry_dachd_i2c_probe,
	.remove		= clk_hifiberry_dachd_i2c_remove,
	.id_table	= clk_hifiberry_dachd_i2c_id,
	.driver		= {
		.name	= "dachd-clk",
		.of_match_table = of_match_ptr(clk_hifiberry_dachd_of_match),
	},
};

module_i2c_driver(clk_hifiberry_dachd_i2c_driver);


MODULE_DESCRIPTION("HiFiBerry DAC+ HD clock driver");
MODULE_AUTHOR("Joerg Schambacher <joerg@i2audio.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clk-hifiberry-dachd");
