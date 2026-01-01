# BeagleBone Black Embedded Linux Drivers

**Production-quality Linux kernel drivers for BeagleBone Black, demonstrating real-world embedded systems development.**

[![Platform](https://img.shields.io/badge/platform-BeagleBone%20Black-blue)](https://beagleboard.org/black)
[![Kernel](https://img.shields.io/badge/kernel-5.10+-green)](https://www.kernel.org/)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue)](LICENSE)

---

## 🎯 **Overview**

This repository showcases hands-on embedded Linux driver development with:
- **Multiple subsystem interfaces**: Character devices, sysfs, input subsystem, IIO framework
- **Real hardware validation**: All drivers tested on physical hardware with oscilloscope/multimeter verification
- **Production-ready practices**: Device tree integration, error handling, proper resource management
- **Professional workflow**: Kernel coding standards, checkpatch compliance, systematic debugging

---

## 📦 **Drivers Included**

### 1. **Button Driver** (Platform + GPIO)
**Three userspace interfaces in one driver:**
- Character device (`/dev/bbb-button`) for human-readable events
- Sysfs attributes for statistics (press count, timestamps, IRQ counters)
- Input subsystem (`/dev/input/eventX`) for standard Linux input events

**Features:**
- ✅ Threaded IRQ handling for debouncing
- ✅ Workqueue-based state machine
- ✅ Atomic counters for statistics
- ✅ Concurrent access handling (waitqueues, spinlocks)
- ✅ Integration with Linux input layer (works with `evtest`)

**Hardware:** GPIO input with IRQ on both edges  
**Documentation:** [Input Subsystem Guide](docs/input-subsystem-driver-guide.md) | [Character Device Guide](docs/character-device-driver-guide.md)

---

### 2. **MCP3008 ADC Driver** (SPI + IIO)
**8-channel 10-bit ADC using the Industrial I/O (IIO) subsystem**

**Features:**
- ✅ SPI protocol implementation for MCP3008 communication
- ✅ IIO channel interface for all 8 ADC channels
- ✅ Voltage reference support (external or internal)
- ✅ Standard IIO sysfs interface (`/sys/bus/iio/devices/iio:deviceX/`)
- ✅ Device tree integration with pinmux configuration

**Hardware:** SPI bus (SCLK, MISO, MOSI, CS)  
**Documentation:** [IIO MCP3008 Guide](docs/iio-mcp3008-driver-guide.md) | [Device Tree Mapping](docs/device-tree-driver-mapping-guide.md)

**Postmortem:** [SPI0 Not Enabled Issue](docs/postmortem-003-spi0-not-enabled.md) - Real debugging story

---

### 3. **TMP117 Temperature Sensor Driver** (I2C + IIO)
**High-precision digital temperature sensor**

**Features:**
- ✅ I2C bus communication
- ✅ IIO framework integration
- ✅ Temperature reading via sysfs
- ✅ Device tree binding

**Hardware:** I2C bus (SDA, SCL)  
**Documentation:** See [IIO MCP3008 Guide](docs/iio-mcp3008-driver-guide.md) for IIO framework concepts

---

## 🛠️ **Build & Deploy**

### Prerequisites
```bash
# Cross-compilation toolchain (Yocto SDK or similar)
arm-poky-linux-gnueabi-gcc

# Kernel headers from Yocto build
# See scripts/fast-build.sh for paths
```

### Quick Build
```bash
# Build a specific driver
./scripts/fast-build.sh button    # Button driver
./scripts/fast-build.sh adc       # MCP3008 driver
./scripts/fast-build.sh tmp117    # TMP117 driver

# Deploy to BBB (over SSH)
./scripts/fast-build.sh deploy-button
./scripts/fast-build.sh deploy-adc
```

### Manual Build
```bash
cd drivers/button
make ARCH=arm CROSS_COMPILE=arm-poky-linux-gnueabi- \
     KDIR=/path/to/kernel-build-artifacts

# Copy to BBB
scp *.ko root@192.168.86.21:/tmp/

# On BBB
insmod /tmp/bbb_flagship_button_combined.ko
```

---

## 📚 **Documentation**

### Driver Development Guides
- **[Character Device Driver Guide](docs/character-device-driver-guide.md)** - Building `/dev` interfaces
- **[Input Subsystem Driver Guide](docs/input-subsystem-driver-guide.md)** - Linux input layer integration
- **[IIO MCP3008 Driver Guide](docs/iio-mcp3008-driver-guide.md)** - Industrial I/O subsystem
- **[Device Tree to Driver Mapping](docs/device-tree-driver-mapping-guide.md)** - DT binding & property extraction
- **[Driver Layers Architecture](docs/driver-layers-architecture-guide.md)** - Bus drivers vs userspace interfaces

### Professional Practices
- **[Kernel Patch Workflow Guide](docs/kernel-patch-workflow-guide.md)** - `checkpatch.pl`, commit messages, patch submission
- **[Professional Git Workflow](docs/professional-git-workflow-guide.md)** - Clean commits, rebasing, collaboration

### Debugging & Troubleshooting
- **[Postmortem: SPI0 Not Enabled](docs/postmortem-003-spi0-not-enabled.md)** - Real debugging case study

---

## 🧪 **Testing**

### Hardware Setup
**BeagleBone Black Rev C with:**
- Button: P8_07 (GPIO2_2) with pull-up resistor
- MCP3008: SPI0 (P9_17, P9_18, P9_21, P9_22)
- TMP117: I2C2 (P9_19, P9_20)

### Automated Tests
```bash
# MCP3008 validation
./scripts/test-mcp3008.sh

# Button validation (manual)
# Press button and observe:
cat /dev/bbb-button                           # Character device
cat /sys/bus/platform/devices/*/press_count   # Sysfs
hexdump -C /dev/input/event4                  # Input events
```

---

## 🏗️ **Architecture Highlights**

### Two-Layer Driver Model
```
┌─────────────────────────────────────────────┐
│         Userspace Applications              │
└─────────────────────────────────────────────┘
              ↕ (ioctl, read, write)
┌─────────────────────────────────────────────┐
│  Layer 2: Userspace Interfaces              │
│  - Character Device (/dev/bbb-button)       │
│  - Sysfs Attributes                         │
│  - Input Subsystem (evdev)                  │
│  - IIO Subsystem                            │
└─────────────────────────────────────────────┘
              ↕ (API calls)
┌─────────────────────────────────────────────┐
│  Layer 1: Bus Drivers                       │
│  - Platform Driver (GPIO/IRQ)               │
│  - SPI Driver (MCP3008)                     │
│  - I2C Driver (TMP117)                      │
└─────────────────────────────────────────────┘
              ↕ (Hardware registers)
┌─────────────────────────────────────────────┐
│           Physical Hardware                 │
└─────────────────────────────────────────────┘
```

See [Driver Layers Architecture Guide](docs/driver-layers-architecture-guide.md) for detailed explanation.

---

## 🔍 **Key Skills Demonstrated**

### Kernel Programming
- ✅ Platform, I2C, and SPI driver frameworks
- ✅ Character device interface with `file_operations`
- ✅ Input subsystem integration
- ✅ IIO (Industrial I/O) framework
- ✅ Device tree bindings and parsing
- ✅ Interrupt handling (threaded IRQs, workqueues)
- ✅ Concurrency (spinlocks, atomic operations, waitqueues)
- ✅ Memory management (devm_* resource-managed APIs)

### Hardware Communication
- ✅ SPI protocol implementation
- ✅ I2C bus communication
- ✅ GPIO interrupt handling
- ✅ Hardware validation with test equipment

### Professional Practices
- ✅ Kernel coding style compliance (`checkpatch.pl`)
- ✅ Proper error handling and resource cleanup
- ✅ Device tree overlay creation and compilation
- ✅ Build automation and testing scripts
- ✅ Documentation and postmortem analysis
- ✅ Systematic debugging methodology

---

## 📁 **Repository Structure**

```
bbb-embedded-drivers/
├── drivers/
│   ├── button/           # Platform + GPIO driver (3 interfaces)
│   ├── mcp3008/          # SPI + IIO driver
│   └── tmp117/           # I2C + IIO driver
├── device-tree/          # Device tree overlays (.dtso)
├── scripts/
│   ├── fast-build.sh     # Automated build & deploy
│   └── test-mcp3008.sh   # Hardware validation script
├── docs/                 # Comprehensive guides
│   ├── *-driver-guide.md # Subsystem-specific guides
│   ├── device-tree-driver-mapping-guide.md
│   ├── driver-layers-architecture-guide.md
│   ├── kernel-patch-workflow-guide.md
│   ├── professional-git-workflow-guide.md
│   └── postmortem-003-spi0-not-enabled.md
└── README.md             # This file
```

---

## 🔗 **Related Technologies**

- **Platform:** BeagleBone Black (AM335x SoC)
- **Kernel:** Linux 5.10+ (Yocto-built)
- **Build System:** Yocto Project (custom BSP layer)
- **Toolchain:** GCC 11.5 (arm-poky-linux-gnueabi)
- **Protocols:** I2C, SPI, GPIO/IRQ
- **Subsystems:** Platform, Input, IIO, Character Device

---

## 📖 **Learning Resources**

These drivers were developed following:
- Linux Device Drivers, 3rd Edition (LDD3)
- Linux Kernel Documentation (`Documentation/driver-api/`)
- Real hardware validation and debugging
- Kernel source code analysis (`drivers/iio/`, `drivers/input/`)

---

## 🤝 **Contributing**

This is a portfolio project, but feedback and suggestions are welcome!

- Open an issue for bugs or questions
- Suggest improvements via pull requests
- Share your own driver implementations

---

## 📜 **License**

GPL-2.0 (same as Linux kernel)

---

## 👤 **Author**

**Chun Sun**  
Embedded Systems Engineer | Linux Kernel Driver Development

- 📧 Email: chunsun978@gmail.com
- 💼 LinkedIn: [linkedin.com/in/chun-sun-1632651b](https://linkedin.com/in/chun-sun-1632651b)
- 🐙 GitHub: [@chunsun978](https://github.com/chunsun978)

---

## 🎯 **What's Next?**

Planned additions:
- PWM driver for LED control
- Watchdog timer driver
- DMA-based SPI transfer optimization
- Real-time kernel (PREEMPT_RT) testing

---

**⭐ If you find this useful, please star the repository!**

