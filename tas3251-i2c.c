// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the TAS3251 CODEC
 *
 * Author:	JPv Coolwijk <jpvc36@gmail.com>
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "tas3251.h"

static int tas3251_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regmap *regmap;
	struct regmap_config config = tas3251_regmap;

	/* msb needs to be set to enable auto-increment of addresses */
	config.read_flag_mask = 0x80;
	config.write_flag_mask = 0x80;

	regmap = devm_regmap_init_i2c(i2c, &config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return tas3251_probe(&i2c->dev, regmap);
}

static int tas3251_i2c_remove(struct i2c_client *i2c)
{
	tas3251_remove(&i2c->dev);
	return 0;
}

static const struct i2c_device_id tas3251_i2c_id[] = {
	{ "tas3251", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas3251_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas3251_of_match[] = {
	{ .compatible = "ti,tas3251", },
	{ }
};
MODULE_DEVICE_TABLE(of, tas3251_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id tas3251_acpi_match[] = {
	{ "104C3251", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, tas3251_acpi_match);
#endif

static struct i2c_driver tas3251_i2c_driver = {
	.probe 		= tas3251_i2c_probe,
	.remove 	= tas3251_i2c_remove,
	.id_table	= tas3251_i2c_id,
	.driver		= {
		.name	= "tas3251",
		.of_match_table = of_match_ptr(tas3251_of_match),
		.acpi_match_table = ACPI_PTR(tas3251_acpi_match),
		.pm     = &tas3251_pm_ops,
	},
};

module_i2c_driver(tas3251_i2c_driver);

MODULE_DESCRIPTION("ASoC TAS3251 codec driver - I2C");
MODULE_AUTHOR("JPv Coolwijk <jpvc36@gmail.com>");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_LICENSE("GPL v2");
