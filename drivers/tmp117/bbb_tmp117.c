#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>

// Register definitions
#define TMP117_REG_TEMP        0x00  // Temperature result register
#define TMP117_REG_CONFIG      0x01  // Configuration register
#define TMP117_REG_DEVICE_ID   0x0F  // Device ID register

// Device ID
#define TMP117_DEVICE_ID       0x0117

// Resolution: 7.8125 mC/LSB = 78125 uC / 10000
#define TMP117_RESOLUTION_NUM  78125
#define TMP117_RESOLUTION_DEN  10000

// Driver private data structure
struct bbb_tmp117_data {
	struct i2c_client *client;
};

// Read temperature from sensor (returns millidegrees Celsius)
static int bbb_tmp117_read_temperature(struct bbb_tmp117_data *data, long *val)
{
	struct i2c_client *client = data->client;
	int reg_val;
	s16 raw;

	reg_val = i2c_smbus_read_word_data(client, TMP117_REG_TEMP);
	if (reg_val < 0) {
		dev_err(&client->dev, "Failed to read temperature: %d\n", reg_val);
		return reg_val;
	}

	// TMP117 is big-endian, SMBus returns little-endian - swap bytes
	raw = swab16(reg_val);

	// Convert to millidegrees Celsius: raw * 7.8125 mC
	// = raw * 78125 / 10000 (using integer math)
	*val = ((long)raw * TMP117_RESOLUTION_NUM) / TMP117_RESOLUTION_DEN;

	return 0;
}

// hwmon read callback
static int bbb_tmp117_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct bbb_tmp117_data *data = dev_get_drvdata(dev);

	if (type != hwmon_temp || attr != hwmon_temp_input || channel != 0)
		return -EOPNOTSUPP;

	return bbb_tmp117_read_temperature(data, val);
}

// hwmon is_visible callback
static umode_t bbb_tmp117_is_visible(const void *data, enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	if (type == hwmon_temp && attr == hwmon_temp_input && channel == 0)
		return 0444;  // Read-only

	return 0;
}

// hwmon operations
static const struct hwmon_ops bbb_tmp117_hwmon_ops = {
	.is_visible = bbb_tmp117_is_visible,
	.read = bbb_tmp117_read,
};

// Channel configuration: one temperature input
static const u32 bbb_tmp117_temp_config[] = {
	HWMON_T_INPUT,
	0
};

static const struct hwmon_channel_info bbb_tmp117_temp_channel = {
	.type = hwmon_temp,
	.config = bbb_tmp117_temp_config,
};

static const struct hwmon_channel_info *bbb_tmp117_channel_info[] = {
	&bbb_tmp117_temp_channel,
	NULL
};

// hwmon chip info
static const struct hwmon_chip_info bbb_tmp117_chip_info = {
	.ops = &bbb_tmp117_hwmon_ops,
	.info = bbb_tmp117_channel_info,
};

// Probe function (old-style signature for kernel < 6.3)
static int bbb_tmp117_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct bbb_tmp117_data *data;
	struct device *hwmon_dev;
	int device_id;

	// Verify device ID
	device_id = i2c_smbus_read_word_data(client, TMP117_REG_DEVICE_ID);
	if (device_id < 0) {
		dev_err(&client->dev, "Failed to read device ID: %d\n", device_id);
		return device_id;
	}
	device_id = swab16(device_id);
	if (device_id != TMP117_DEVICE_ID) {
		dev_err(&client->dev, "Unexpected device ID: 0x%04x\n", device_id);
		return -ENODEV;
	}

	// Allocate driver data
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);

	// Register hwmon device
	hwmon_dev = devm_hwmon_device_register_with_info(&client->dev,
							 "bbb_tmp117",
							 data,
							 &bbb_tmp117_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(&client->dev, "BBB TMP117 temperature sensor initialized\n");
	return 0;
}

// I2C device ID table
static const struct i2c_device_id bbb_tmp117_id[] = {
	{ "bbb_tmp117", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bbb_tmp117_id);

// Device tree match table - use custom compatible string
static const struct of_device_id bbb_tmp117_of_match[] = {
	{ .compatible = "bbb,tmp117" },  // Custom: avoids conflict with ti,tmp117
	{ }
};
MODULE_DEVICE_TABLE(of, bbb_tmp117_of_match);

// I2C driver structure
static struct i2c_driver bbb_tmp117_driver = {
	.driver = {
		.name = "bbb_tmp117",
		.of_match_table = bbb_tmp117_of_match,
	},
	.probe = bbb_tmp117_probe,
	.id_table = bbb_tmp117_id,
};
module_i2c_driver(bbb_tmp117_driver);

MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("BBB Flagship TMP117 Temperature Sensor Driver");
MODULE_LICENSE("GPL");

