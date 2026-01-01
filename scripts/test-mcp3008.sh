#!/bin/bash

echo "==================================="
echo "MCP3008 Driver Test Script"
echo "==================================="
echo ""

# Phase 1: Check SPI hardware
echo "[1/5] Checking SPI hardware..."
if [ -d "/sys/class/spi_master/spi0" ]; then
    echo "✅ SPI0 controller found"
else
    echo "❌ SPI0 controller NOT found"
    echo "   → Load device tree overlay or enable SPI0"
    exit 1
fi

# Phase 2: Check SPI device
echo "[2/5] Checking SPI device..."
if [ -d "/sys/bus/spi/devices/spi0.0" ]; then
    echo "✅ SPI device spi0.0 found"
    echo "   Modalias: $(cat /sys/bus/spi/devices/spi0.0/modalias)"
else
    echo "❌ SPI device spi0.0 NOT found"
    echo "   → Check device tree overlay"
    exit 1
fi

# Phase 3: Check driver
echo "[3/5] Checking driver..."
if [ -d "/sys/bus/spi/drivers/mcp3008" ]; then
    echo "✅ MCP3008 driver loaded"
else
    echo "❌ MCP3008 driver NOT loaded"
    echo "   → Run: insmod /tmp/bbb_mcp3008.ko"
    exit 1
fi

# Phase 4: Check driver binding
echo "[4/5] Checking driver binding..."
if [ -L "/sys/bus/spi/drivers/mcp3008/spi0.0" ]; then
    echo "✅ Driver bound to spi0.0"
else
    echo "❌ Driver NOT bound"
    echo "   → Check dmesg for probe errors"
    exit 1
fi

# Phase 5: Check IIO device
echo "[5/5] Checking IIO device..."
IIO_DEV=$(ls -d /sys/bus/iio/devices/iio:device* 2>/dev/null | head -n1)
if [ -n "$IIO_DEV" ]; then
    echo "✅ IIO device found: $IIO_DEV"
    
    # Test reading
    if [ -f "$IIO_DEV/in_voltage0_raw" ]; then
        echo ""
        echo "==================================="
        echo "ADC Readings:"
        echo "==================================="
        SCALE=$(cat $IIO_DEV/in_voltage_scale)
        for ch in {0..7}; do
            RAW=$(cat $IIO_DEV/in_voltage${ch}_raw)
            VOLTAGE=$(echo "scale=2; $RAW * $SCALE" | bc)
            echo "Channel $ch: Raw=$RAW  Voltage=${VOLTAGE}mV"
        done
        echo ""
        echo "✅ SUCCESS! MCP3008 driver is working!"
    else
        echo "❌ Cannot read ADC channels"
        exit 1
    fi
else
    echo "❌ IIO device NOT found"
    echo "   → Check dmesg for registration errors"
    exit 1
fi

