# IIO Driver Guide: MCP3008 ADC

**Goal:** Implement a professional IIO driver for the MCP3008 8-channel 10-bit ADC  
**Target:** BeagleBone Black with Linux 6.1.80-ti  
**Subsystems:** IIO (Industrial I/O), SPI  
**Author:** Chun  
**Date:** December 28, 2025

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware: MCP3008 ADC](#hardware-mcp3008-adc)
3. [IIO Subsystem Architecture](#iio-subsystem-architecture)
4. [BeagleBone Black SPI Configuration](#beaglebone-black-spi-configuration)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Step-by-Step Implementation](#step-by-step-implementation)
7. [Testing and Validation](#testing-and-validation)
8. [Advanced Features](#advanced-features)
9. [Troubleshooting](#troubleshooting)

---

## Overview

### What is IIO?

The **Industrial I/O (IIO)** subsystem is the standard Linux framework for sensor drivers. It provides:

- **Standardized interface**: All sensors expose data through `/sys/bus/iio/`
- **Channel abstraction**: Multi-channel devices (ADC, IMU) handled elegantly
- **Triggered buffers**: High-speed continuous data acquisition
- **Userspace tools**: `iio_info`, `iio_readdev` for testing
- **Hardware agnostic**: Works with SPI, I2C, platform devices

### Why IIO for Sensors?

| Feature | Traditional Character Device | IIO Subsystem |
|---------|------------------------------|---------------|
| **Standard API** | Custom per-driver | Unified across all sensors |
| **Metadata** | Manual | Automatic (scale, offset, units) |
| **Multi-channel** | Complex implementation | Built-in support |
| **Buffered I/O** | Manual ring buffer | Framework-provided |
| **Triggers** | No standard | Hardware/software triggers |
| **Userspace Tools** | Custom apps | Standard tools work |

**For sensor embedded developers, IIO is essential.**

---

## Hardware: MCP3008 ADC

### Specifications

- **Manufacturer**: Microchip (not TI - common confusion!)
- **Channels**: 8 single-ended or 4 differential
- **Resolution**: 10-bit (0-1023 values)
- **Sample Rate**: 200 ksps max
- **Interface**: SPI (Mode 0,0 or 1,1)
- **Voltage**: 2.7V - 5.5V supply
- **Reference**: Single supply (Vref = Vdd)
- **Package**: 16-pin DIP or SOIC

### Pin Configuration

```bash
        MCP3008
    ┌────────────┐
CH0 │1        16 │ VDD  (3.3V or 5V)
CH1 │2        15 │ VREF (typically tied to VDD)
CH2 │3        14 │ AGND (Analog Ground)
CH3 │4        13 │ CLK  (SPI Clock)
CH4 │5        12 │ DOUT (MISO)
CH5 │6        11 │ DIN  (MOSI)
CH6 │7        10 │ CS   (Chip Select, active low)
CH7 │8         9 │ DGND (Digital Ground)
    └────────────┘
```

### Connection to BeagleBone Black

**BBB SPI0 (default):**

| MCP3008 Pin | BBB Pin | Function |
|-------------|---------|----------|
| VDD (16) | P9_3 | 3.3V |
| VREF (15) | P9_3 | 3.3V (or external reference) |
| AGND (14) | P9_1 | GND |
| DGND (9) | P9_1 | GND |
| CLK (13) | P9_22 | SPI0_SCLK |
| DOUT (12) | P9_21 | SPI0_MISO |
| DIN (11) | P9_18 | SPI0_MOSI |
| CS (10) | P9_17 | SPI0_CS0 |

**Important Notes:**
- Connect both AGND and DGND to BBB ground
- For 5V operation, use P9_5/P9_7 (VDD_5V) and level shifters
- For best accuracy, use external stable Vref

---

## IIO Subsystem Architecture

### Core Concepts

#### 1. IIO Device (`struct iio_dev`)

The main device structure allocated with `iio_device_alloc()`:

```c
struct iio_dev *indio_dev;
indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(struct mcp3008_data));
```

#### 2. IIO Channels (`struct iio_chan_spec`)

Describes each channel (ADC input, accelerometer axis, etc.):

```c
static const struct iio_chan_spec mcp3008_channels[] = {
    {
        .type = IIO_VOLTAGE,              // Type of measurement
        .indexed = 1,                     // Has an index (CH0, CH1, etc.)
        .channel = 0,                     // Channel number
        .address = 0,                     // Hardware address (for read_raw)
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),  // Per-channel attributes
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), // Shared attributes
    },
    // ... 7 more channels
};
```

#### 3. IIO Info (`struct iio_info`)

Callback functions for channel operations:

```c
static const struct iio_info mcp3008_info = {
    .read_raw = mcp3008_read_raw,        // Read channel value
    .write_raw = mcp3008_write_raw,      // Write (for writeable channels)
};
```

#### 4. Sysfs Interface

Once registered, IIO creates a structured sysfs tree:

```bash
/sys/bus/iio/devices/iio:device0/
├── name                          # Device name: "mcp3008"
├── in_voltage0_raw               # Channel 0 raw value (0-1023)
├── in_voltage1_raw               # Channel 1 raw value
├── ...
├── in_voltage7_raw               # Channel 7 raw value
├── in_voltage_scale              # Shared scale factor
├── dev                           # Device number
├── sampling_frequency            # (optional) sample rate control
└── buffer/                       # (optional) buffered mode
    ├── enable
    ├── length
    └── ...
```

### Reading a Channel

**From userspace:**
```bash
# Read raw ADC value (0-1023)
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
# Output: 512

# Read scale factor (volts per bit)
cat /sys/bus/iio/devices/iio:device0/in_voltage_scale
# Output: 0.003225806  # (3.3V / 1024)

# Calculate actual voltage:
# voltage = raw * scale = 512 * 0.003225806 = 1.651V
```

**From code (driver side):**
```c
static int mcp3008_read_raw(struct iio_dev *indio_dev,
                           struct iio_chan_spec const *chan,
                           int *val, int *val2, long mask)
{
    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        // Read ADC value for chan->channel
        *val = mcp3008_adc_read(chan->channel);
        return IIO_VAL_INT;
        
    case IIO_CHAN_INFO_SCALE:
        // Return scale as val.val2 (e.g., 3.3V / 1024)
        *val = 3300;  // millivolts
        *val2 = 1024; // resolution
        return IIO_VAL_FRACTIONAL;
    }
    return -EINVAL;
}
```

---

## MCP3008 SPI Protocol

### Communication Format

The MCP3008 uses a 3-byte SPI transaction:

```c
Byte 1 (TX): Start bit + Config
Byte 2 (TX): Channel selection
Byte 3 (TX): Don't care

Byte 1 (RX): Don't care
Byte 2 (RX): MSB of result (bits 9-8) + padding
Byte 3 (RX): LSB of result (bits 7-0)
```

### Command Format

**Single-ended mode** (measure CHx relative to GND):

```c
TX[0] = 0x01                      // Start bit
TX[1] = 0x80 | (channel << 4)     // Single-ended + channel
TX[2] = 0x00                      // Don't care

RX[0] = XX                        // Ignore
RX[1] = 0x00 | (bit9 << 0) | (bit8 << 1) // Top 2 bits
RX[2] = bits[7:0]                 // Low 8 bits

result = ((RX[1] & 0x03) << 8) | RX[2];
```

**Example: Read Channel 3**

```c
uint8_t tx[3] = {0x01, 0xB0, 0x00};  // 0xB0 = 0x80 | (3 << 4)
uint8_t rx[3];

spi_write_then_read(spi, tx, 3, rx, 3);

int value = ((rx[1] & 0x03) << 8) | rx[2];  // 10-bit result (0-1023)
```

### SPI Mode and Timing

- **Mode**: 0 (CPOL=0, CPHA=0) or 3 (CPOL=1, CPHA=1)
- **Clock**: Up to 3.6 MHz (Vdd=5V) or 1.35 MHz (Vdd=2.7V)
- **CS**: Active low, must stay low during entire transaction

---

## BeagleBone Black SPI Configuration

### Device Tree Overlay

Create: `dts/overlays/mcp3008-spi0.dtso`

```c
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-black";

    /* Identify this overlay */
    part-number = "MCP3008-SPI0";
    version = "00A0";

    /* Fragment to enable SPI0 */
    fragment@0 {
        target = <&spi0>;
        __overlay__ {
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;

            /* MCP3008 device on SPI0, CS0 */
            mcp3008@0 {
                compatible = "microchip,mcp3008";
                reg = <0>;  /* CS0 */
                spi-max-frequency = <1000000>;  /* 1 MHz for safety */
                vref-supply = <&vdd_3v3>;       /* Reference voltage */
            };
        };
    };
};
```

### Pin Mux Configuration

SPI0 pins on BBB are typically already muxed correctly, but verify:

```bash
# Check pinmux
cat /sys/kernel/debug/pinctrl/44e10800.pinmux-pinctrl-single/pins | grep -i spi

# Expected:
# pin 39 (PIN39): spi0_sclk (GPIO0_3)
# pin 40 (PIN40): spi0_miso (GPIO0_4)
# pin 41 (PIN41): spi0_mosi (GPIO0_5)
# pin 42 (PIN42): spi0_cs0  (GPIO0_7)
```

### Compile and Deploy DTBO

```bash
# Compile
dtc -@ -I dts -O dtb -o mcp3008-spi0.dtbo dts/overlays/mcp3008-spi0.dtso

# Copy to BBB
scp mcp3008-spi0.dtbo root@192.168.86.21:/lib/firmware/

# Load overlay (on BBB)
echo "mcp3008-spi0" > /sys/kernel/config/device-tree/overlays/mcp3008/path

# Or add to /boot/uEnv.txt for auto-load:
# uboot_overlay_addr0=/lib/firmware/mcp3008-spi0.dtbo
```

---

## Implementation Roadmap

### Phase 1: Basic Driver Structure (30 min)
- [x] Create driver skeleton
- [x] SPI probe/remove
- [x] Device tree binding
- [x] Module loading test

### Phase 2: IIO Integration (1 hour)
- [x] Allocate `iio_dev`
- [x] Define 8 channels (`iio_chan_spec`)
- [x] Implement `read_raw()` callback
- [x] Register IIO device
- [x] Test single channel read

### Phase 3: MCP3008 Protocol (45 min)
- [x] Implement SPI read function
- [x] Channel selection logic
- [x] Data parsing (10-bit result)
- [x] Test all 8 channels

### Phase 4: Calibration and Scale (30 min)
- [x] Voltage reference handling
- [x] Scale calculation
- [x] Offset support (if needed)
- [x] Test voltage calculations

### Phase 5: Advanced Features (Optional)
- [ ] Differential mode support
- [ ] Triggered buffers
- [ ] Hardware triggers
- [ ] DMA for high-speed sampling

**Total Time: ~3 hours for basic driver, +2 hours for advanced**

---

## Step-by-Step Implementation

### Step 1: Create Driver Skeleton

**File:** `kernel/drivers/adc/mcp3008.c`

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * MCP3008 8-channel 10-bit ADC driver
 *
 * Author: Chun
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

#define MCP3008_CHANNELS 8

struct mcp3008 {
    struct spi_device *spi;
    struct regulator *vref;
    u16 vref_mv;  /* Reference voltage in millivolts */
};

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

    /* Store for later */
    spi_set_drvdata(spi, indio_dev);

    dev_info(&spi->dev, "MCP3008 ADC probed\n");
    return 0;
}

static void mcp3008_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "MCP3008 ADC removed\n");
}

static const struct of_device_id mcp3008_dt_ids[] = {
    { .compatible = "microchip,mcp3008" },
    { }
};
MODULE_DEVICE_TABLE(of, mcp3008_dt_ids);

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
```

### Step 2: Define IIO Channels

Add channel definitions:

```c
#define MCP3008_CHANNEL(chan) {                         \
    .type = IIO_VOLTAGE,                                \
    .indexed = 1,                                       \
    .channel = (chan),                                  \
    .address = (chan),                                  \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),       \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
}

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
```

### Step 3: Implement ADC Read Function

```c
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
    tx[0] = 0x01;                      /* Start bit */
    tx[1] = 0x80 | (channel << 4);     /* Single-ended + channel select */
    tx[2] = 0x00;                      /* Don't care */

    ret = spi_sync_transfer(adc->spi, &xfer, 1);
    if (ret < 0)
        return ret;

    /* Extract 10-bit result */
    return ((rx[1] & 0x03) << 8) | rx[2];
}
```

### Step 4: Implement IIO read_raw Callback

```c
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
```

### Step 5: Complete Probe Function

```c
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

static void mcp3008_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    struct mcp3008 *adc = iio_priv(indio_dev);

    if (!IS_ERR(adc->vref))
        regulator_disable(adc->vref);

    dev_info(&spi->dev, "MCP3008 ADC removed\n");
}
```

### Step 6: Create Makefile

**File:** `kernel/drivers/adc/Makefile`

```makefile
obj-m := mcp3008.o

# For out-of-tree build
KERNEL_BUILD ?= /path/to/kernel/build

all:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) clean
```

---

## Testing and Validation

### Build and Deploy

```bash
# On DELL host
cd /home/chun/projects/buildBBBWithYocto
./scripts/fast-build.sh # Update to support ADC driver

# Or manual
cd kernel/drivers/adc
export CROSS_COMPILE=arm-poky-linux-gnueabi-
export ARCH=arm
export KERNEL_BUILD=/path/to/kernel-build-artifacts
make

# Copy to BBB
scp mcp3008.ko root@192.168.86.21:/tmp/

# On BBB
insmod /tmp/mcp3008.ko
```

### Verify Driver Loaded

```bash
# Check module
lsmod | grep mcp3008

# Check dmesg
dmesg | tail -5
# Expected: "MCP3008 ADC registered (vref=3300mV)"

# Check IIO device created
ls /sys/bus/iio/devices/
# Expected: iio:device0 (or deviceN)

# Check device name
cat /sys/bus/iio/devices/iio:device0/name
# Expected: mcp3008
```

### Test Single Channel Read

```bash
# Read raw value (0-1023)
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
# Example output: 512

# Read scale
cat /sys/bus/iio/devices/iio:device0/in_voltage_scale
# Example output: 0.003225806 (3300mV / 1024)

# Calculate voltage
# voltage = raw * scale = 512 * 0.003225806 = 1.651V
```

### Test All Channels

```bash
# Simple test script
for ch in {0..7}; do
    raw=$(cat /sys/bus/iio/devices/iio:device0/in_voltage${ch}_raw)
    echo "Channel $ch: $raw"
done

# Expected output:
# Channel 0: 512
# Channel 1: 0
# Channel 2: 1023
# ...
```

### Hardware Test with Voltage Divider

Connect a simple voltage divider to CH0:

```properties
3.3V ──┬── 10kΩ ──┬── CH0
       │          │
       │         10kΩ
       │          │
      GND        GND

Expected voltage at CH0: 1.65V
Expected raw value: ~512 (1.65V / 3.3V * 1024)
```

### Using IIO Utilities (if installed)

```bash
# Install on BBB (if available)
opkg install libiio libiio-utils

# List devices
iio_info

# Read channel
iio_readdev -s 10 mcp3008 voltage0
```

---

## Advanced Features

### 1. Differential Mode

Modify channel definition to support differential:

```properties
#define MCP3008_DIFF_CHANNEL(chan, chan2) {             \
    .type = IIO_VOLTAGE,                                \
    .indexed = 1,                                       \
    .channel = (chan),                                  \
    .channel2 = (chan2),                                \
    .address = (chan),                                  \
    .differential = 1,                                  \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),       \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
}
```

Modify command byte:
```c
tx[1] = (channel << 4);  /* Differential mode (bit 7 = 0) */
```

### 2. Triggered Buffers

For continuous high-speed sampling:

```c
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

static irqreturn_t mcp3008_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct mcp3008 *adc = iio_priv(indio_dev);
    u16 data[8];
    int i;

    /* Read all enabled channels */
    for (i = 0; i < 8; i++) {
        if (test_bit(i, indio_dev->active_scan_mask))
            data[i] = mcp3008_adc_conversion(adc, i);
    }

    iio_push_to_buffers(indio_dev, data);
    iio_trigger_notify_done(indio_dev->trig);

    return IRQ_HANDLED;
}

/* In probe: */
ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
                                     NULL,
                                     mcp3008_trigger_handler,
                                     NULL);
```

### 3. Custom Sampling Rate

Add sampling frequency control:

```c
/* In channel definition */
.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),

/* In read_raw/write_raw */
case IIO_CHAN_INFO_SAMP_FREQ:
    *val = adc->sampling_freq;
    return IIO_VAL_INT;
```

---

## Troubleshooting

### Issue: Driver Not Probing

**Symptoms:** Module loads but no `iio:device` created

**Check:**
```bash
# Is SPI bus enabled?
ls /sys/bus/spi/devices/
# Should show: spi0.0

# Device tree loaded?
ls /sys/firmware/devicetree/base/spi@*/mcp3008*

# Driver bound?
ls /sys/bus/spi/drivers/mcp3008/
```

**Solutions:**
- Verify device tree overlay loaded
- Check compatible string matches
- Ensure SPI0 enabled in device tree

### Issue: All Reads Return 0 or 1023

**Problem:** Incorrect SPI mode or wiring

**Check:**
```bash
# SPI mode in device tree
cat /sys/class/spi_master/spi0/spi0.0/mode
# Should be 0x0 or 0x3

# Check connections with multimeter
# Verify CLK, MISO, MOSI, CS all connected
```

**Solutions:**
- Double-check wiring against pinout
- Try different SPI mode in DT
- Add pull-up on CS line

### Issue: Noisy Readings

**Problem:** ADC values fluctuate wildly

**Solutions:**
- Add 0.1uF capacitor between Vref and GND
- Add 10uF capacitor between VDD and GND
- Use twisted pair or shielded wire for analog inputs
- Enable input low-pass filter in hardware
- Average multiple readings in software

### Issue: Scale Incorrect

**Problem:** Voltage calculation doesn't match multimeter

**Check:**
```bash
# What's the actual Vref?
cat /sys/bus/iio/devices/iio:device0/in_voltage_scale

# Measure Vref with multimeter
# Should match vref-supply in device tree
```

**Solutions:**
- Verify vref-supply in device tree
- Measure actual reference voltage
- Update device tree with correct value

---

## Next Steps

### Immediate (This Session)
1. ✅ Read this guide
2. [ ] Wire MCP3008 to BBB
3. [ ] Create device tree overlay
4. [ ] Implement basic driver
5. [ ] Test single channel read

### Short Term (Next Session)
1. [ ] Test all 8 channels
2. [ ] Add voltage divider test circuit
3. [ ] Verify scale and calculations
4. [ ] Document any issues

### Advanced (Optional)
1. [ ] Implement differential mode
2. [ ] Add triggered buffers
3. [ ] Create simple data logger app
4. [ ] Benchmark sampling speed

---

## Resources

### Kernel Documentation
- `Documentation/driver-api/iio/`
- `Documentation/devicetree/bindings/iio/adc/`
- `Documentation/spi/`

### Example Drivers in Kernel Tree
- `drivers/iio/adc/mcp320x.c` - Similar ADC (great reference!)
- `drivers/iio/adc/ti-ads1015.c` - I2C ADC example
- `drivers/iio/adc/ad7476.c` - SPI ADC example

### IIO Tools
- `tools/iio/` in kernel source
- `libiio` - Userspace library

### MCP3008 Datasheet
- Microchip document DS21295C
- Key sections: 5.0 (SPI interface), 6.0 (Timing)

---

## Summary

This guide provides everything needed to implement a professional IIO driver for the MCP3008 ADC. Key takeaways:

✅ **IIO subsystem** provides standardized sensor framework  
✅ **Channel abstraction** handles multi-channel devices elegantly  
✅ **Sysfs interface** gives immediate userspace access  
✅ **SPI protocol** is straightforward for MCP3008  
✅ **Device tree** configures hardware without code changes  
✅ **Scale/offset** provides automatic unit conversion  

**Ready to start coding? Let's build the driver!** 🚀

---

*Author: Chun*  
*Date: December 28, 2025*  
*Target: BeagleBone Black + MCP3008*

