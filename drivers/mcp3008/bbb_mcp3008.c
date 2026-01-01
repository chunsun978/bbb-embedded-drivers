/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCP3008 8-channel 10-bit ADC driver for BeagleBone Black
 *
 * Author: Chun
 * Date: December 28, 2025
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

#define MCP3008_CHANNELS 8

/* Driver private data */
struct mcp3008 {
	struct spi_device *spi;
	struct regulator *vref;
	u16 vref_mv;  /* Reference voltage in millivolts */
};

/* Helper macro to define IIO channels */
#define MCP3008_CHANNEL(chan) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (chan),					\
	.address = (chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

/* Define 8 channels */
static const struct iio_chan_spec mcp3008_channels[] = {
	MCP3008_CHANNEL(0),
	MCP3008_CHANNEL(1),
	MCP3008_CHANNEL(2),
	MCP3008_CHANNEL(3),
	MCP3008_CHANNEL(4),
	MCP3008_CHANNEL(5),
	MCP3008_CHANNEL(6),
	MCP3008_CHANNEL(7),
};

/**
 * mcp3008_adc_conversion - Read ADC value from specified channel
 * @adc: MCP3008 device structure
 * @channel: Channel number (0-7)
 *
 * Returns: 10-bit ADC value (0-1023) on success, negative error code on failure
 */
static int mcp3008_adc_conversion(struct mcp3008 *adc, u8 channel)
{
	u8 tx[3], rx[3];
	int ret;
	struct spi_transfer xfer = {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = 3,
	};

	/* Prepare command: single-ended mode */
	tx[0] = 0x01;			/* Start bit */
	tx[1] = 0x80 | (channel << 4);	/* Single-ended + channel select */
	tx[2] = 0x00;			/* Don't care */

	ret = spi_sync_transfer(adc->spi, &xfer, 1);
	if (ret < 0)
		return ret;

	/* Extract 10-bit result */
	return ((rx[1] & 0x03) << 8) | rx[2];
}

/**
 * mcp3008_read_raw - IIO callback for reading channel data
 */
static int mcp3008_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct mcp3008 *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp3008_adc_conversion(adc, chan->address);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		/* Scale: (vref_mv) / 1024 in millivolts */
		*val = adc->vref_mv;
		*val2 = 10;  /* Divide by 2^10 = 1024 */
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static const struct iio_info mcp3008_info = {
	.read_raw = mcp3008_read_raw,
};

/**
 * mcp3008_probe - Initialize the MCP3008 device
 */
static int mcp3008_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct mcp3008 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	/* Get voltage reference (or default to 3.3V) */
	adc->vref = devm_regulator_get_optional(&spi->dev, "vref");
	if (IS_ERR(adc->vref)) {
		if (PTR_ERR(adc->vref) == -ENODEV) {
			/* No regulator specified, assume 3.3V */
			adc->vref_mv = 3300;
		} else {
			return PTR_ERR(adc->vref);
		}
	} else {
		ret = regulator_enable(adc->vref);
		if (ret)
			return ret;

		ret = regulator_get_voltage(adc->vref);
		if (ret < 0)
			goto err_vref_disable;

		adc->vref_mv = ret / 1000; /* Convert uV to mV */
	}

	spi_set_drvdata(spi, indio_dev);

	/* Configure IIO device */
	indio_dev->name = "mcp3008";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mcp3008_channels;
	indio_dev->num_channels = ARRAY_SIZE(mcp3008_channels);
	indio_dev->info = &mcp3008_info;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		goto err_vref_disable;

	dev_info(&spi->dev, "MCP3008 ADC registered (vref=%umV)\n", adc->vref_mv);
	return 0;

err_vref_disable:
	if (!IS_ERR(adc->vref))
		regulator_disable(adc->vref);
	return ret;
}

/**
 * mcp3008_remove - Clean up when driver is removed
 */
static void mcp3008_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct mcp3008 *adc = iio_priv(indio_dev);

	if (!IS_ERR(adc->vref))
		regulator_disable(adc->vref);

	dev_info(&spi->dev, "MCP3008 ADC removed\n");
}

/* Device tree match table */
static const struct of_device_id mcp3008_dt_ids[] = {
	{ .compatible = "microchip,mcp3008" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp3008_dt_ids);

/* SPI device ID table */
static const struct spi_device_id mcp3008_id[] = {
	{ "mcp3008", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp3008_id);

static struct spi_driver mcp3008_driver = {
	.driver = {
		.name = "mcp3008",
		.of_match_table = mcp3008_dt_ids,
	},
	.probe = mcp3008_probe,
	.remove = mcp3008_remove,
	.id_table = mcp3008_id,
};

module_spi_driver(mcp3008_driver);

MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("MCP3008 8-channel 10-bit ADC driver");
MODULE_LICENSE("GPL");
