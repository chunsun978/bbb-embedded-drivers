# Device Tree to Driver Mapping Guide

**Author:** Chun  
**Date:** December 28, 2025  
**Purpose:** Complete reference for understanding how device tree nodes bind to Linux kernel drivers

---

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [The Binding Mechanism](#the-binding-mechanism)
3. [Platform Devices (Memory-Mapped)](#platform-devices-memory-mapped)
4. [I2C Devices](#i2c-devices)
5. [SPI Devices](#spi-devices)
6. [GPIO Subsystem](#gpio-subsystem)
7. [Real Project Examples](#real-project-examples)
8. [Debugging Binding Issues](#debugging-binding-issues)
9. [Common Patterns Reference](#common-patterns-reference)

---

## Core Concepts

### What is Device Tree?

Device tree is a **data structure** that describes hardware configuration to the Linux kernel at boot time, avoiding the need to hard-code device information in the kernel.

```c
Hardware → Device Tree → Kernel Driver → Userspace
   ↓            ↓             ↓              ↓
 Physical    .dts/.dtb    .c/.ko      /sys, /dev
```

### Key Terminology

| Term | Definition | Example |
|------|------------|---------|
| **DTS** | Device Tree Source (human-readable) | `bbb-flagship-button.dtso` |
| **DTB** | Device Tree Blob (binary, loaded by kernel) | `bbb-flagship-button.dtbo` |
| **DTBO** | Device Tree Blob Overlay (runtime) | Applied over base DTB |
| **Compatible String** | Unique ID that binds DT node to driver | `"bbb,flagship-button"` |
| **Node** | A device or hardware component in DT | `bbb-flagship-button { ... }` |
| **Property** | Configuration parameter in a node | `compatible`, `reg`, `interrupts` |
| **Phandle** | Reference to another node | `<&gpio2 3 GPIO_ACTIVE_LOW>` |

---

## The Binding Mechanism

### How Kernel Matches Device Tree to Driver

```bash
Boot Process:
1. Bootloader (U-Boot) loads DTB into memory
2. Kernel parses DTB during early boot
3. For each DT node, kernel extracts "compatible" string
4. Kernel searches registered drivers for matching "compatible"
5. If match found → driver's probe() function is called
6. Driver initializes hardware using DT properties
```

### The Compatible String Match

This is the **heart** of DT-to-driver binding.

**In Device Tree:**
```c
my_device {
    compatible = "vendor,device-model";
    /* ... other properties ... */
};
```

**In Driver Code:**
```c
static const struct of_device_id my_driver_dt_ids[] = {
    { .compatible = "vendor,device-model" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_driver_dt_ids);

static struct platform_driver my_driver = {
    .driver = {
        .name = "my-driver",
        .of_match_table = my_driver_dt_ids,  /* ← Match happens here */
    },
    .probe = my_probe,
    .remove = my_remove,
};
```

### Match Algorithm

The kernel uses **best-match** algorithm:

```c
compatible = "vendor,specific-v2", "vendor,specific", "vendor,generic";
```

Kernel tries to match in order:
1. `"vendor,specific-v2"` ← Most specific
2. `"vendor,specific"`
3. `"vendor,generic"` ← Most generic (fallback)

**First matching driver wins** and gets to call `probe()`.

---

## Platform Devices (Memory-Mapped)

Platform devices are simple devices that don't sit on a standard bus (I2C, SPI, USB). They're typically memory-mapped peripherals or GPIOs.

### Architecture

```bash
Device Tree Node
       ↓
of_device_id match (compatible string)
       ↓
platform_driver.probe() called
       ↓
platform_device struct created
       ↓
Driver accesses resources (GPIO, IRQ, etc.)
```

### Complete Example: BBB Flagship Button

#### Device Tree (DTS)

**File:** `dts/overlays/bbb-flagship-button.dtso`

```c
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-black";

    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            bbb_button_pins: bbb_button_pins {
                pinctrl-single,pins = <
                    0x078 0x37  /* P9_12 GPIO1_28, INPUT | PULLUP */
                >;
            };
        };
    };

    fragment@1 {
        target-path = "/";
        __overlay__ {
            bbb-flagship-button {
                compatible = "bbb,flagship-button";  /* ← KEY: This must match driver */
                label = "BBB Flagship Button";
                gpios = <&gpio1 28 GPIO_ACTIVE_LOW>;
                linux,code = <KEY_ENTER>;
                debounce-interval = <20>;
                pinctrl-names = "default";
                pinctrl-0 = <&bbb_button_pins>;
            };
        };
    };
};
```

**Key Properties Explained:**

| Property | Type | Purpose | Driver Access |
|----------|------|---------|---------------|
| `compatible` | string | **REQUIRED**: Binds to driver | Used for matching only |
| `label` | string | Human-readable name | `of_get_property()` |
| `gpios` | phandle | Reference to GPIO controller + pin | `devm_gpiod_get()` |
| `linux,code` | u32 | Input event code (KEY_ENTER=28) | `of_property_read_u32()` |
| `debounce-interval` | u32 | Debounce time in ms | `of_property_read_u32()` |
| `pinctrl-names` | string | Pinmux configuration names | Handled by pinctrl subsystem |
| `pinctrl-0` | phandle | Reference to pinmux settings | Handled by pinctrl subsystem |

#### Driver Code (C)

**File:** `kernel/drivers/button/bbb_flagship_button.c`

```c
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>

/* Step 1: Define compatible string match table */
static const struct of_device_id bbb_btn_dt_ids[] = {
    { .compatible = "bbb,flagship-button" },  /* ← Must match DT exactly */
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bbb_btn_dt_ids);  /* Export for module autoloading */

/* Step 2: Probe function - called when DT node matches */
static int bbb_btn_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;  /* Pointer to DT node */
    struct bbb_btn *b;
    u32 debounce_ms = 20;  /* default */
    int ret;

    dev_info(dev, "Device tree node: %s\n", np->full_name);

    /* Allocate driver data */
    b = devm_kzalloc(dev, sizeof(*b), GFP_KERNEL);
    if (!b)
        return -ENOMEM;

    /* Extract GPIO from device tree "gpios" property */
    b->gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
    if (IS_ERR(b->gpiod))
        return dev_err_probe(dev, PTR_ERR(b->gpiod), "Failed to get GPIO\n");

    /* Extract custom property: debounce-interval */
    of_property_read_u32(np, "debounce-interval", &debounce_ms);
    b->debounce_ms = debounce_ms;

    /* Extract label (optional) */
    of_property_read_string(np, "label", &b->label);

    /* Get IRQ number from GPIO */
    b->irq = gpiod_to_irq(b->gpiod);
    if (b->irq < 0)
        return dev_err_probe(dev, b->irq, "Failed to get IRQ\n");

    /* Request threaded IRQ */
    ret = devm_request_threaded_irq(dev, b->irq, bbb_btn_irq,
                                     NULL, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                                     "bbb_flagship_button", b);
    if (ret)
        return dev_err_probe(dev, ret, "Failed to request IRQ\n");

    /* Store driver data */
    platform_set_drvdata(pdev, b);

    dev_info(dev, "Probed successfully (GPIO=%d, IRQ=%d, debounce=%ums)\n",
             desc_to_gpio(b->gpiod), b->irq, debounce_ms);

    return 0;
}

/* Step 3: Remove function */
static int bbb_btn_remove(struct platform_device *pdev)
{
    struct bbb_btn *b = platform_get_drvdata(pdev);
    
    dev_info(&pdev->dev, "Driver removed\n");
    /* devm_* resources are automatically freed */
    
    return 0;
}

/* Step 4: Register platform driver */
static struct platform_driver bbb_btn_driver = {
    .probe = bbb_btn_probe,
    .remove = bbb_btn_remove,
    .driver = {
        .name = "bbb_flagship_button",
        .of_match_table = bbb_btn_dt_ids,  /* ← Links DT to driver */
    },
};

module_platform_driver(bbb_btn_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("BeagleBone Black Flagship Button Driver");
```

#### Runtime Binding Verification

```bash
# 1. Check if overlay is loaded
cat /proc/device-tree/bbb-flagship-button/compatible
# Output: bbb,flagship-button

# 2. Check if driver is loaded
lsmod | grep bbb_flagship_button
# Output: bbb_flagship_button  20480  0

# 3. Check if driver registered
ls /sys/bus/platform/drivers/bbb_flagship_button
# Output: bind  bbb-flagship-button  module  uevent  unbind
#         ↑                          ↑
#    Control files              Bound device

# 4. Check if device is bound to driver
ls -l /sys/bus/platform/drivers/bbb_flagship_button/bbb-flagship-button
# Output: lrwxrwxrwx ... -> ../../../../devices/platform/bbb-flagship-button

# 5. Check probe success in dmesg
dmesg | grep bbb_flagship_button
# Output: bbb_flagship_button bbb-flagship-button: Probed successfully (GPIO=60, IRQ=52, debounce=20ms)
```

---

## I2C Devices

I2C devices are connected to an I2C bus controller and addressed by their I2C slave address.

### Architecture

```bash
I2C Controller Node (in base DT)
       ↓
i2c_driver.probe() called for each child node
       ↓
i2c_client struct created (contains I2C address)
       ↓
Driver uses i2c_smbus_* or i2c_transfer() functions
```

### Complete Example: TMP117 Temperature Sensor

#### Device Tree (DTS)

**File:** `dts/overlays/bbb-flagship-tmp117-i2c2.dtso`

```c
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-black";

    /* Fragment 0: Configure I2C2 pins */
    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            i2c2_pins: i2c2_pins {
                pinctrl-single,pins = <
                    0x178 0x73  /* P9_18 I2C2_SDA, INPUT | PULLUP | MODE3 */
                    0x17C 0x73  /* P9_17 I2C2_SCL, INPUT | PULLUP | MODE3 */
                >;
            };
        };
    };

    /* Fragment 1: Enable I2C2 controller and add TMP117 device */
    fragment@1 {
        target = <&i2c2>;  /* ← Reference to existing I2C2 controller */
        __overlay__ {
            status = "okay";
            pinctrl-names = "default";
            pinctrl-0 = <&i2c2_pins>;
            clock-frequency = <100000>;  /* 100 kHz */

            /* TMP117 device as child of I2C2 bus */
            tmp117@48 {  /* @48 is I2C slave address (0x48) */
                compatible = "ti,tmp117";  /* ← KEY: Must match driver */
                reg = <0x48>;  /* I2C address (7-bit) */
                label = "BBB Flagship TMP117";
            };
        };
    };
};
```

**Key I2C-Specific Properties:**

| Property | Type | Purpose | Value in Example |
|----------|------|---------|------------------|
| `reg` | u32 | **REQUIRED**: I2C 7-bit address | `<0x48>` |
| `compatible` | string | **REQUIRED**: Binds to driver | `"ti,tmp117"` |
| `target` | phandle | Parent I2C controller | `<&i2c2>` |
| `clock-frequency` | u32 | I2C bus speed (Hz) | `100000` (100 kHz) |

#### Driver Code (C)

**File:** `kernel/drivers/tmp117/bbb_tmp117.c`

```c
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

/* TMP117 Register Addresses */
#define TMP117_REG_TEMP     0x00  /* Temperature result register */
#define TMP117_REG_CONFIG   0x01  /* Configuration register */
#define TMP117_REG_DEVICE_ID 0x0F /* Device ID register (0x0117) */

struct tmp117_data {
    struct i2c_client *client;
    struct device *hwmon_dev;
    char label[32];
};

/* Step 1: Define compatible string match table */
static const struct of_device_id tmp117_dt_ids[] = {
    { .compatible = "ti,tmp117" },  /* ← Must match DT */
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tmp117_dt_ids);

/* Also support old-style I2C device ID matching (optional) */
static const struct i2c_device_id tmp117_id[] = {
    { "tmp117", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, tmp117_id);

/* Step 2: Probe function - called when DT node matches */
static int tmp117_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    struct device *dev = &client->dev;
    struct device_node *np = dev->of_node;
    struct tmp117_data *data;
    u16 device_id;
    int ret;

    dev_info(dev, "Probing TMP117 at I2C address 0x%02x\n", client->addr);

    /* Verify this is actually a TMP117 by reading device ID */
    ret = i2c_smbus_read_word_swapped(client, TMP117_REG_DEVICE_ID);
    if (ret < 0)
        return dev_err_probe(dev, ret, "Failed to read device ID\n");
    
    device_id = ret;
    if (device_id != 0x0117) {
        dev_err(dev, "Invalid device ID: 0x%04x (expected 0x0117)\n", device_id);
        return -ENODEV;
    }

    /* Allocate driver data */
    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;

    /* Extract optional label from DT */
    if (of_property_read_string(np, "label", (const char **)&data->label))
        strcpy(data->label, "TMP117");

    /* Store driver data */
    i2c_set_clientdata(client, data);

    dev_info(dev, "TMP117 probed successfully (ID=0x%04x, label=%s)\n",
             device_id, data->label);

    return 0;
}

/* Step 3: Remove function */
static void tmp117_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "TMP117 driver removed\n");
}

/* Step 4: Register I2C driver */
static struct i2c_driver tmp117_driver = {
    .driver = {
        .name = "bbb_tmp117",
        .of_match_table = tmp117_dt_ids,  /* ← Links DT to driver */
    },
    .probe = tmp117_probe,
    .remove = tmp117_remove,
    .id_table = tmp117_id,
};

module_i2c_driver(tmp117_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("TI TMP117 Temperature Sensor Driver");
```

#### How to Read Temperature

```c
/* Read 16-bit temperature register (big-endian) */
static int tmp117_read_temp(struct i2c_client *client)
{
    s32 ret;
    s16 raw_temp;
    int temp_mc;  /* Temperature in millidegrees Celsius */

    /* Read 16-bit value from register 0x00 */
    ret = i2c_smbus_read_word_swapped(client, TMP117_REG_TEMP);
    if (ret < 0)
        return ret;

    raw_temp = (s16)ret;

    /* TMP117 resolution: 7.8125 m°C per LSB = 78125/10000000 */
    temp_mc = ((long)raw_temp * 78125) / 1000;

    return temp_mc;  /* Returns e.g., 25000 for 25.000°C */
}
```

#### Runtime Binding Verification

```bash
# 1. Check if I2C2 is enabled
ls /sys/bus/i2c/devices/
# Output: i2c-0  i2c-1  i2c-2  2-0048
#                        ↑       ↑
#                   Controller  Device at address 0x48

# 2. Check device node in DT
ls /proc/device-tree/ocp/i2c@4819c000/tmp117@48/
# Output: compatible  label  name  reg

cat /proc/device-tree/ocp/i2c@4819c000/tmp117@48/compatible
# Output: ti,tmp117

# 3. Check if driver bound to device
ls -l /sys/bus/i2c/devices/2-0048/driver
# Output: lrwxrwxrwx ... -> ../../../bus/i2c/drivers/bbb_tmp117

# 4. Verify probe in dmesg
dmesg | grep tmp117
# Output: bbb_tmp117 2-0048: TMP117 probed successfully (ID=0x0117, label=BBB Flagship TMP117)

# 5. Detect device on I2C bus (manual check)
i2cdetect -y -r 2
# Output:
#      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
# 00:          -- -- -- -- -- -- -- -- -- -- -- -- --
# 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
# 20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
# 30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
# 40: -- -- -- -- -- -- -- -- 48 -- -- -- -- -- -- --
#                               ↑
#                          TMP117 detected!
```

---

## SPI Devices

SPI devices are connected to an SPI bus controller and use chip select (CS) lines for addressing.

### Architecture

```bash
SPI Controller Node (in base DT)
       ↓
spi_driver.probe() called for each child node
       ↓
spi_device struct created (contains CS number, max frequency)
       ↓
Driver uses spi_sync(), spi_write(), spi_read() functions
```

### Complete Example: MCP3008 ADC

#### Device Tree (DTS)

**File:** `dts/overlays/bbb-mcp3008-complete.dtso`

```c
/dts-v1/;
/plugin/;

#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/pinctrl/am33xx.h>

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-black";

    /* Fragment 0: Configure SPI0 pins */
    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            spi0_pins: spi0_pins {
                pinctrl-single,pins = <
                    AM33XX_PADCONF(AM335X_PIN_SPI0_SCLK, PIN_INPUT_PULLUP, MUX_MODE0)  /* P9_22 */
                    AM33XX_PADCONF(AM335X_PIN_SPI0_D0,   PIN_INPUT_PULLUP, MUX_MODE0)  /* P9_21 MISO */
                    AM33XX_PADCONF(AM335X_PIN_SPI0_D1,   PIN_OUTPUT_PULLUP, MUX_MODE0) /* P9_18 MOSI */
                    AM33XX_PADCONF(AM335X_PIN_SPI0_CS0,  PIN_OUTPUT_PULLUP, MUX_MODE0) /* P9_17 CS0 */
                >;
            };
        };
    };

    /* Fragment 1: Enable SPI0 controller and add MCP3008 device */
    fragment@1 {
        target = <&spi0>;  /* ← Reference to existing SPI0 controller */
        __overlay__ {
            status = "okay";
            pinctrl-names = "default";
            pinctrl-0 = <&spi0_pins>;

            /* MCP3008 device as child of SPI0 bus */
            mcp3008@0 {  /* @0 is chip select number (CS0) */
                compatible = "microchip,mcp3008";  /* ← KEY: Must match driver */
                reg = <0>;  /* Chip select: CS0 */
                spi-max-frequency = <1000000>;  /* 1 MHz */
                vref-supply = <&vcc_3v3>;  /* Reference voltage regulator */
                label = "BBB MCP3008 ADC";
            };
        };
    };
};
```

**Key SPI-Specific Properties:**

| Property | Type | Purpose | Value in Example |
|----------|------|---------|------------------|
| `reg` | u32 | **REQUIRED**: Chip select number | `<0>` (CS0) |
| `compatible` | string | **REQUIRED**: Binds to driver | `"microchip,mcp3008"` |
| `spi-max-frequency` | u32 | **REQUIRED**: Max SPI clock (Hz) | `1000000` (1 MHz) |
| `target` | phandle | Parent SPI controller | `<&spi0>` |
| `vref-supply` | phandle | Voltage reference (for ADC) | `<&vcc_3v3>` |

#### Driver Code (C)

**File:** `kernel/drivers/adc/bbb_mcp3008.c`

```c
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

/* MCP3008 has 8 single-ended channels */
#define MCP3008_NUM_CHANNELS 8

struct mcp3008 {
    struct spi_device *spi;
    struct regulator *vref;
    int vref_mv;
};

/* Step 1: Define compatible string match table */
static const struct of_device_id mcp3008_dt_ids[] = {
    { .compatible = "microchip,mcp3008" },  /* ← Must match DT */
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mcp3008_dt_ids);

/* Also support SPI device ID matching (optional) */
static const struct spi_device_id mcp3008_id[] = {
    { "mcp3008", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, mcp3008_id);

/* SPI transaction to read MCP3008 channel */
static int mcp3008_adc_conversion(struct mcp3008 *adc, int channel)
{
    u8 tx[3], rx[3];
    int ret;
    u16 raw;

    /* MCP3008 SPI protocol:
     * TX: [START|SGL/DIFF|D2|D1|D0|X|X|X] [X|X|X|X|X|X|X|X] [X|X|X|X|X|X|X|X]
     * RX: [X|X|X|X|X|X|X|X] [NULL|B9|B8|B7|B6|B5|B4|B3] [B2|B1|B0|X|X|X|X|X]
     */
    tx[0] = 0x01;  /* Start bit */
    tx[1] = (0x08 | channel) << 4;  /* Single-ended, channel select */
    tx[2] = 0x00;

    /* Perform SPI transfer */
    ret = spi_write_then_read(adc->spi, tx, sizeof(tx), rx, sizeof(rx));
    if (ret < 0)
        return ret;

    /* Extract 10-bit result */
    raw = ((rx[1] & 0x03) << 8) | rx[2];

    return raw;  /* 0-1023 */
}

/* Step 2: Probe function - called when DT node matches */
static int mcp3008_probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct device_node *np = dev->of_node;
    struct iio_dev *indio_dev;
    struct mcp3008 *adc;
    int ret;

    dev_info(dev, "Probing MCP3008 on SPI bus %d, CS %d\n",
             spi->master->bus_num, spi->chip_select);

    /* Allocate IIO device */
    indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
    if (!indio_dev)
        return -ENOMEM;

    adc = iio_priv(indio_dev);
    adc->spi = spi;

    /* Get voltage reference from DT (vref-supply property) */
    adc->vref = devm_regulator_get(dev, "vref");
    if (IS_ERR(adc->vref))
        return dev_err_probe(dev, PTR_ERR(adc->vref),
                             "Failed to get vref regulator\n");

    ret = regulator_enable(adc->vref);
    if (ret)
        return dev_err_probe(dev, ret, "Failed to enable vref\n");

    /* Read vref voltage */
    ret = regulator_get_voltage(adc->vref);
    if (ret < 0) {
        regulator_disable(adc->vref);
        return dev_err_probe(dev, ret, "Failed to get vref voltage\n");
    }
    adc->vref_mv = ret / 1000;  /* Convert µV to mV */

    /* Configure IIO device */
    indio_dev->name = "mcp3008";
    indio_dev->modes = INDIO_DIRECT_MODE;
    /* ... more IIO setup ... */

    /* Register IIO device */
    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        regulator_disable(adc->vref);
        return ret;
    }

    dev_info(dev, "MCP3008 registered (vref=%dmV, max_freq=%dHz)\n",
             adc->vref_mv, spi->max_speed_hz);

    return 0;
}

/* Step 3: Remove function */
static void mcp3008_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    struct mcp3008 *adc = iio_priv(indio_dev);

    regulator_disable(adc->vref);
    dev_info(&spi->dev, "MCP3008 driver removed\n");
}

/* Step 4: Register SPI driver */
static struct spi_driver mcp3008_driver = {
    .driver = {
        .name = "mcp3008",
        .of_match_table = mcp3008_dt_ids,  /* ← Links DT to driver */
    },
    .probe = mcp3008_probe,
    .remove = mcp3008_remove,
    .id_table = mcp3008_id,
};

module_spi_driver(mcp3008_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("Microchip MCP3008 ADC Driver");
```

#### Runtime Binding Verification

```bash
# 1. Check if SPI0 is enabled
ls /sys/bus/spi/devices/
# Output: spi0.0
#         ↑  ↑
#       Bus CS

# 2. Check SPI master
ls /sys/class/spi_master/
# Output: spi0

# 3. Check device node in DT
cat /proc/device-tree/ocp/spi@48030000/mcp3008@0/compatible
# Output: microchip,mcp3008

# 4. Check if driver bound
ls -l /sys/bus/spi/devices/spi0.0/driver
# Output: lrwxrwxrwx ... -> ../../../bus/spi/drivers/mcp3008

# 5. Check IIO device created
ls /sys/bus/iio/devices/
# Output: iio:device0

cat /sys/bus/iio/devices/iio:device0/name
# Output: mcp3008

# 6. Verify probe in dmesg
dmesg | grep mcp3008
# Output: mcp3008 spi0.0: MCP3008 registered (vref=3300mV, max_freq=1000000Hz)

# 7. Test reading ADC channel 0
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
# Output: 512 (example reading)
```

---

## GPIO Subsystem

GPIO properties can be referenced from device tree using phandles.

### GPIO Property Syntax

```c
device {
    gpios = <&gpio_controller pin_number flags>;
    /*       ↑                ↑           ↑
     *    Phandle to      Pin number   Active high/low,
     *    GPIO controller  on that     pull-up/down, etc.
     *                     controller
     */
};
```

### Example: GPIO References

```c
/ {
    /* GPIO controllers (already defined in base DT) */
    gpio0: gpio@44e07000 { /* Bank 0 */ };
    gpio1: gpio@4804c000 { /* Bank 1 */ };
    gpio2: gpio@481ac000 { /* Bank 2 */ };
    gpio3: gpio@481ae000 { /* Bank 3 */ };

    /* Your device using GPIOs */
    my_button {
        compatible = "my-company,button";
        
        /* Single GPIO */
        gpios = <&gpio1 28 GPIO_ACTIVE_LOW>;
        /*       ↑      ↑  ↑
         *    GPIO1   Pin  Active low (0 = pressed)
         *   (bank 1)  28
         */

        /* Multiple GPIOs with names */
        reset-gpios = <&gpio2 5 GPIO_ACTIVE_HIGH>;
        enable-gpios = <&gpio0 10 GPIO_ACTIVE_HIGH>;
    };
};
```

### Driver Code: Accessing GPIOs

```c
#include <linux/gpio/consumer.h>

static int my_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct gpio_desc *gpiod;
    struct gpio_desc *reset_gpio;
    
    /* Method 1: Get unnamed GPIO (uses "gpios" property) */
    gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
    if (IS_ERR(gpiod))
        return PTR_ERR(gpiod);

    /* Method 2: Get named GPIO (uses "<name>-gpios" property) */
    reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(reset_gpio))
        return PTR_ERR(reset_gpio);

    /* Use the GPIO */
    gpiod_set_value_cansleep(reset_gpio, 1);  /* Assert reset */
    msleep(10);
    gpiod_set_value_cansleep(reset_gpio, 0);  /* Release reset */

    /* Read GPIO state (handles ACTIVE_LOW automatically) */
    int state = gpiod_get_value(gpiod);  /* 1 = active, 0 = inactive */

    return 0;
}
```

### GPIO Flags

| Flag | Value | Meaning |
|------|-------|---------|
| `GPIO_ACTIVE_HIGH` | 0 | Logic high = active state |
| `GPIO_ACTIVE_LOW` | 1 | Logic low = active state (inverted) |
| `GPIO_OPEN_DRAIN` | 2 | Open-drain output |
| `GPIO_OPEN_SOURCE` | 4 | Open-source output |
| `GPIO_PULL_UP` | 8 | Enable internal pull-up |
| `GPIO_PULL_DOWN` | 16 | Enable internal pull-down |

---

## Real Project Examples

### Summary of Your Drivers

| Driver | Bus Type | DT Compatible | DT File | Driver File |
|--------|----------|---------------|---------|-------------|
| **Button** | Platform (GPIO) | `"bbb,flagship-button"` | `bbb-flagship-button.dtso` | `bbb_flagship_button.c` |
| **TMP117** | I2C | `"ti,tmp117"` | `bbb-flagship-tmp117-i2c2.dtso` | `bbb_tmp117.c` |
| **MCP3008** | SPI | `"microchip,mcp3008"` | `bbb-mcp3008-complete.dtso` | `bbb_mcp3008.c` |

### Full System Architecture

```bash
Device Tree Overlays:
┌────────────────────────────────────────────────────────────────┐
│ bbb-flagship-button.dtso                                       │
│   └─► compatible = "bbb,flagship-button"                       │
│       gpios = <&gpio1 28 GPIO_ACTIVE_LOW>                      │
│                                                                │
│ bbb-flagship-tmp117-i2c2.dtso                                  │
│   └─► i2c2 {                                                   │
│         tmp117@48 {                                            │
│           compatible = "ti,tmp117";                            │
│           reg = <0x48>;  /* I2C address */                     │
│         }                                                      │
│       }                                                        │
│                                                                │
│ bbb-mcp3008-complete.dtso                                      │
│   └─► spi0 {                                                   │
│         mcp3008@0 {                                            │
│           compatible = "microchip,mcp3008";                    │
│           reg = <0>;  /* Chip select CS0 */                    │
│           spi-max-frequency = <1000000>;                       │
│         }                                                      │
│       }                                                        │
└────────────────────────────────────────────────────────────────┘
                              ↓
                    Kernel Device Matching
                              ↓
┌────────────────────────────────────────────────────────────────┐
│ Drivers:                                                       │
│                                                                │
│ bbb_flagship_button.ko                                         │
│   of_device_id[] = { "bbb,flagship-button" }                   │
│   └─► platform_driver                                          │
│       └─► Sysfs: /sys/.../press_count                          │
│       └─► Chardev: /dev/bbb-button                             │
│       └─► Input: /dev/input/event0                             │
│                                                                │
│ bbb_tmp117.ko                                                  │
│   of_device_id[] = { "ti,tmp117" }                             │
│   └─► i2c_driver                                               │
│       └─► Hwmon: /sys/class/hwmon/hwmon0/temp1_input           │
│                                                                │
│ bbb_mcp3008.ko                                                 │
│   of_device_id[] = { "microchip,mcp3008" }                     │
│   └─► spi_driver                                               │
│       └─► IIO: /sys/bus/iio/devices/iio:device0/in_voltage*    │
└────────────────────────────────────────────────────────────────┘
```

---

## Debugging Binding Issues

### Problem: Driver Loads But Doesn't Probe

**Symptoms:**
```bash
lsmod | grep my_driver
# Shows: my_driver  16384  0  ✅ (loaded)

ls /sys/bus/platform/drivers/my_driver/
# Shows: bind  module  uevent  unbind  ❌ (no device bound)

dmesg | grep my_driver
# Shows: (nothing)  ❌ (probe never called)
```

**Root Cause:** Compatible string mismatch between DT and driver.

**Debugging Steps:**

#### Step 1: Check Device Tree Node

```bash
# List all top-level device nodes
ls /proc/device-tree/

# Check if your device node exists
ls /proc/device-tree/my-device/

# Read compatible string from device node
cat /proc/device-tree/my-device/compatible
# Or use hexdump to see null-terminated strings
hexdump -C /proc/device-tree/my-device/compatible
```

#### Step 2: Check Driver Compatible Strings

```bash
# Extract compatible strings from driver module
modinfo my_driver.ko | grep alias
# Output: alias:          of:N*T*Cvendor,device-nameC*
#                                  ↑
#                         This is what driver expects
```

#### Step 3: Compare and Fix

```bash
# Device Tree has:        "vendor,device-name"
# Driver expects:         "vendor,device-name"
#                         ↑
# These MUST match exactly (case-sensitive!)
```

**Common Mistakes:**

1. **Typo in compatible string**
   ```dts
   compatible = "bbb,flaghsip-button";  /* ❌ "flaghsip" */
   compatible = "bbb,flagship-button";  /* ✅ Correct */
   ```

2. **Wrong vendor prefix**
   ```dts
   compatible = "chun,flagship-button";  /* ❌ */
   compatible = "bbb,flagship-button";   /* ✅ */
   ```

3. **Comma vs dash confusion**
   ```dts
   compatible = "bbb-flagship-button";  /* ❌ No comma */
   compatible = "bbb,flagship-button";  /* ✅ Has comma */
   ```

4. **Multiple compatible strings (order matters)**
   ```dts
   compatible = "bbb,flagship-button-v2", "bbb,flagship-button";
   /* ↑ Driver must have at least one of these */
   ```

### Problem: Device Tree Overlay Not Loading

```bash
# Check overlay exists
ls /lib/firmware/*.dtbo

# Try to load manually (if configfs supported)
mkdir /sys/kernel/config/device-tree/overlays/test
cat my-overlay.dtbo > /sys/kernel/config/device-tree/overlays/test/dtbo
# Error messages will appear in dmesg

# Check dmesg for errors
dmesg | grep -i "device tree"
```

**Common Errors:**
- Syntax error in DTS (fix with `dtc -O dtb -o test.dtbo test.dtso`)
- Missing target node (e.g., `target = <&i2c2>;` but i2c2 doesn't exist)
- Pinmux conflict (another driver already claimed the pin)

### Problem: Wrong Bus Number or Address

**I2C Example:**
```bash
# Device tree says:
#   i2c2 { tmp117@48 { ... } }

# But actual hardware is on I2C1 at address 0x49

# You'll see in dmesg:
# i2c i2c-2: probe error: -6 (ENXIO - No such device or address)

# Fix: Update DT to correct bus and address
```

**SPI Example:**
```bash
# Device tree says:
#   spi0 { mcp3008@0 { spi-max-frequency = <10000000>; } }

# But MCP3008 max is 3.6 MHz

# You'll get garbled data or errors

# Fix: Reduce frequency to 1000000 (1 MHz) for safety
```

---

## Common Patterns Reference

### Pattern 1: Simple GPIO Device (Platform)

```c
my_led {
    compatible = "gpio-leds";  /* Uses existing kernel driver */
    
    led0 {
        label = "my-led";
        gpios = <&gpio1 21 GPIO_ACTIVE_HIGH>;
        linux,default-trigger = "heartbeat";
    };
};
```

No custom driver needed! Uses `drivers/leds/leds-gpio.c`.

### Pattern 2: I2C Sensor with Interrupt

```c
i2c1 {
    sensor@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;
        interrupt-parent = <&gpio2>;
        interrupts = <25 IRQ_TYPE_EDGE_FALLING>;
    };
};
```

Driver accesses interrupt:
```c
int irq = of_irq_get(np, 0);
```

### Pattern 3: SPI with Multiple CS Lines

```c
spi0 {
    device1@0 {
        compatible = "vendor,device1";
        reg = <0>;  /* CS0 */
        spi-max-frequency = <1000000>;
    };

    device2@1 {
        compatible = "vendor,device2";
        reg = <1>;  /* CS1 */
        spi-max-frequency = <500000>;
    };
};
```

### Pattern 4: Regulator Supply

```c
vcc_3v3: regulator-3v3 {
    compatible = "regulator-fixed";
    regulator-name = "vcc_3v3";
    regulator-min-microvolt = <3300000>;
    regulator-max-microvolt = <3300000>;
};

adc@0 {
    compatible = "microchip,mcp3008";
    vref-supply = <&vcc_3v3>;  /* Phandle reference */
};
```

Driver access:
```c
struct regulator *vref = devm_regulator_get(dev, "vref");
int vref_uv = regulator_get_voltage(vref);
```

### Pattern 5: Pinmux Configuration

```c
am33xx_pinmux: pinmux@44e10800 {
    my_device_pins: my_device_pins {
        pinctrl-single,pins = <
            0x154 0x07  /* Offset 0x154, MODE7, INPUT, PULLUP */
        >;
    };
};

my_device {
    compatible = "vendor,my-device";
    pinctrl-names = "default";
    pinctrl-0 = <&my_device_pins>;  /* Applied automatically at probe */
};
```

---

## Summary: The Complete Flow

```c
1. Hardware Design
   ↓
2. Write Device Tree (.dtso)
   - Define compatible string
   - Add bus-specific properties (reg, spi-max-frequency, etc.)
   - Add device-specific properties
   ↓
3. Compile Device Tree
   $ dtc -@ -I dts -O dtb -o my-device.dtbo my-device.dtso
   ↓
4. Write Driver (.c)
   - Create of_device_id[] with compatible string
   - Implement probe() function
   - Extract properties using of_property_read_*()
   - Register driver with module_*_driver()
   ↓
5. Deploy
   - Copy .dtbo to /lib/firmware/
   - Load overlay (uEnv.txt or manual)
   - Load driver (insmod or auto-load)
   ↓
6. Kernel Matching
   - Kernel parses DT node
   - Finds driver with matching compatible string
   - Calls driver->probe()
   ↓
7. Driver Initializes
   - Reads DT properties
   - Configures hardware
   - Creates device files (/dev, /sys)
   ↓
8. Userspace Access
   - Read/write through device files
   - Application works!
```

---

## Quick Reference Card

### Device Tree Template

```c
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone-black";

    fragment@0 {
        target = <&am33xx_pinmux>;  /* For pinmux */
        __overlay__ {
            my_pins: my_pins {
                pinctrl-single,pins = < /* ... */ >;
            };
        };
    };

    fragment@1 {
        target-path = "/";  /* For platform device */
        /* OR */
        target = <&i2c2>;   /* For I2C device */
        /* OR */
        target = <&spi0>;   /* For SPI device */
        
        __overlay__ {
            my_device {
                compatible = "vendor,device-name";  /* ← KEY */
                /* Add other properties */
            };
        };
    };
};
```

### Driver Template

```c
#include <linux/module.h>
#include <linux/of.h>

static const struct of_device_id my_dt_ids[] = {
    { .compatible = "vendor,device-name" },  /* ← Must match DT */
    { }
};
MODULE_DEVICE_TABLE(of, my_dt_ids);

static int my_probe(struct XXX_device *dev)
{
    /* Initialize hardware using DT properties */
    return 0;
}

static void my_remove(struct XXX_device *dev)
{
    /* Cleanup */
}

static struct XXX_driver my_driver = {
    .driver = {
        .name = "my-driver",
        .of_match_table = my_dt_ids,
    },
    .probe = my_probe,
    .remove = my_remove,
};

module_XXX_driver(my_driver);  /* platform/i2c/spi */

MODULE_LICENSE("GPL");
```

---

## Further Reading

- [Device Tree Specification](https://www.devicetree.org/)
- [Linux Kernel Documentation: Device Tree](https://docs.kernel.org/devicetree/)
- [Device Tree Overlays](https://docs.kernel.org/devicetree/overlay-notes.html)
- [BeagleBone Device Tree](https://github.com/beagleboard/BeagleBoard-DeviceTrees)

---

**Document Status:** Complete and ready for reference  
**Last Updated:** December 28, 2025

---

