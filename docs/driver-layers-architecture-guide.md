# Linux Driver Layered Architecture Guide

**Author:** Chun  
**Date:** December 29, 2025  
**Purpose:** Deep dive into the separation between bus drivers (Layer 1) and userspace interfaces (Layer 2)

---

## Table of Contents

1. [Overview: The Two-Layer Model](#overview-the-two-layer-model)
2. [Layer 1: Bus Drivers (Hardware Communication)](#layer-1-bus-drivers-hardware-communication)
3. [Layer 2: Userspace Interfaces](#layer-2-userspace-interfaces)
4. [Complete Examples](#complete-examples)
5. [Kernel Subsystems (The Smart Alternative)](#kernel-subsystems-the-smart-alternative)
6. [When to Use What](#when-to-use-what)
7. [Advanced Patterns](#advanced-patterns)
8. [Real-World Case Studies](#real-world-case-studies)
9. [Best Practices](#best-practices)

---

## Overview: The Two-Layer Model

### The Big Picture

Linux drivers are typically structured in **two distinct layers**:

```bash
┌─────────────────────────────────────────────────────────────┐
│                    Userspace Application                    │
│                    (your C/Python program)                  │
└──────────────────────────┬──────────────────────────────────┘
                           │
                   System Call Interface
                   (read, write, ioctl)
                           │
┌──────────────────────────┴──────────────────────────────────┐
│               LAYER 2: Userspace Interface                  │
│                                                             │
│  ┌─────────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │ Character Device│  │   Subsystem  │  │  Sysfs/Procfs  │  │
│  │  /dev/mydev     │  │ (IIO/hwmon/  │  │  /sys/.../attr │  │
│  │                 │  │  input/etc)  │  │                │  │
│  │ file_operations │  │              │  │                │  │
│  └─────────────────┘  └──────────────┘  └────────────────┘  │
└──────────────────────────┬──────────────────────────────────┘
                           │
                   Internal Kernel API
                           │
┌──────────────────────────┴──────────────────────────────────┐
│             LAYER 1: Bus Driver (Hardware Layer)            │
│                                                             │
│  ┌────────────────┐  ┌────────────┐  ┌──────────────────┐   │
│  │ Platform Driver│  │ I2C Driver │  │   SPI Driver     │   │
│  │                │  │            │  │                  │   │
│  │ probe/remove   │  │ i2c_smbus  │  │  spi_transfer    │   │
│  │ GPIO/IRQ       │  │ i2c_client │  │  spi_device      │   │
│  └────────────────┘  └────────────┘  └──────────────────┘   │
└──────────────────────────┬──────────────────────────────────┘
                           │
                    Hardware Registers
                           │
┌──────────────────────────┴──────────────────────────────────┐
│                    Physical Hardware                        │
│              (GPIO, I2C bus, SPI bus, etc.)                 │
└─────────────────────────────────────────────────────────────┘
```

### Why Two Layers?

**Separation of Concerns:**

| Layer | Responsibility | Skills Required |
|-------|----------------|-----------------|
| **Layer 1** | Hardware communication, bus protocol, device initialization | Hardware knowledge, timing, protocols |
| **Layer 2** | Userspace API, data formatting, access control | Userspace API design, concurrency |

**Benefits:**

1. **Modularity**: Can swap Layer 2 implementations without changing Layer 1
2. **Reusability**: Same Layer 1 can support multiple Layer 2 interfaces
3. **Maintainability**: Clear separation makes code easier to understand
4. **Flexibility**: Can use standard subsystems or custom interfaces

---

## Layer 1: Bus Drivers (Hardware Communication)

### Purpose

Layer 1 handles **all hardware-specific operations**:
- Device initialization and configuration
- Bus communication (I2C reads/writes, SPI transfers, GPIO manipulation)
- Interrupt handling
- Power management
- Device tree binding
- Hardware resource management

### Common Layer 1 Driver Types

#### 1. Platform Drivers

For simple memory-mapped devices or GPIO-based devices.

**Structure:**

```c
struct platform_driver {
    int (*probe)(struct platform_device *);
    int (*remove)(struct platform_device *);
    void (*shutdown)(struct platform_device *);
    int (*suspend)(struct platform_device *, pm_message_t state);
    int (*resume)(struct platform_device *);
    struct device_driver driver;
};
```

**No `.fops` field!** ❌

**Example: GPIO Button**

```c
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>

struct button_data {
    struct gpio_desc *gpiod;
    int irq;
    unsigned long press_count;
};

/* Layer 1: Hardware interrupt handler */
static irqreturn_t button_irq_handler(int irq, void *dev_id)
{
    struct button_data *data = dev_id;
    
    /* Read GPIO state (hardware access) */
    int state = gpiod_get_value(data->gpiod);
    
    if (state == 0)  /* Active low */
        data->press_count++;
    
    return IRQ_HANDLED;
}

/* Layer 1: Probe - Initialize hardware */
static int button_probe(struct platform_device *pdev)
{
    struct button_data *data;
    int ret;
    
    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    /* Get GPIO from device tree */
    data->gpiod = devm_gpiod_get(&pdev->dev, NULL, GPIOD_IN);
    if (IS_ERR(data->gpiod))
        return PTR_ERR(data->gpiod);
    
    /* Get IRQ from GPIO */
    data->irq = gpiod_to_irq(data->gpiod);
    if (data->irq < 0)
        return data->irq;
    
    /* Request interrupt */
    ret = devm_request_irq(&pdev->dev, data->irq, button_irq_handler,
                           IRQF_TRIGGER_FALLING, "button", data);
    if (ret)
        return ret;
    
    platform_set_drvdata(pdev, data);
    
    dev_info(&pdev->dev, "Button driver initialized (GPIO IRQ=%d)\n", data->irq);
    return 0;
}

/* Layer 1: Remove - Cleanup hardware */
static int button_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Button driver removed\n");
    /* devm_* automatically frees resources */
    return 0;
}

/* Layer 1: Platform driver registration */
static const struct of_device_id button_dt_ids[] = {
    { .compatible = "my-company,button" },
    { }
};
MODULE_DEVICE_TABLE(of, button_dt_ids);

static struct platform_driver button_driver = {
    .probe = button_probe,
    .remove = button_remove,
    .driver = {
        .name = "my-button",
        .of_match_table = button_dt_ids,
    },
    /* ❌ NO .fops here! */
};

module_platform_driver(button_driver);
```

**What Layer 1 Does:**
- ✅ Binds to device tree node
- ✅ Acquires GPIO resource
- ✅ Sets up interrupt handler
- ✅ Tracks button presses in kernel memory
- ❌ Does NOT provide userspace access yet!

#### 2. I2C Drivers

For devices on the I2C bus.

**Structure:**

```c
struct i2c_driver {
    unsigned int class;
    int (*probe)(struct i2c_client *, const struct i2c_device_id *);
    void (*remove)(struct i2c_client *);
    void (*shutdown)(struct i2c_client *);
    int (*suspend)(struct i2c_client *, pm_message_t mesg);
    int (*resume)(struct i2c_client *);
    struct device_driver driver;
    const struct i2c_device_id *id_table;
    /* ... */
};
```

**No `.fops` field!** ❌

**Example: Temperature Sensor (TMP117)**

```c
#include <linux/i2c.h>
#include <linux/module.h>

#define TMP117_REG_TEMP     0x00
#define TMP117_REG_CONFIG   0x01
#define TMP117_REG_DEVICE_ID 0x0F

struct tmp117_data {
    struct i2c_client *client;
    int last_temp_mc;  /* millidegrees Celsius */
};

/* Layer 1: Read temperature from hardware via I2C */
static int tmp117_read_temperature(struct tmp117_data *data)
{
    s32 ret;
    s16 raw_temp;
    
    /* I2C bus read operation */
    ret = i2c_smbus_read_word_swapped(data->client, TMP117_REG_TEMP);
    if (ret < 0)
        return ret;
    
    raw_temp = (s16)ret;
    
    /* Convert to millidegrees (TMP117: 7.8125 m°C per LSB) */
    data->last_temp_mc = ((long)raw_temp * 78125) / 1000;
    
    return 0;
}

/* Layer 1: Configure hardware via I2C */
static int tmp117_configure(struct tmp117_data *data)
{
    s32 ret;
    u16 config;
    
    /* Read current config */
    ret = i2c_smbus_read_word_swapped(data->client, TMP117_REG_CONFIG);
    if (ret < 0)
        return ret;
    
    config = ret;
    
    /* Set continuous conversion mode */
    config &= ~0x0C00;  /* Clear conversion mode bits */
    config |= 0x0000;   /* Continuous conversion */
    
    /* Write config back */
    ret = i2c_smbus_write_word_swapped(data->client, TMP117_REG_CONFIG, config);
    if (ret < 0)
        return ret;
    
    return 0;
}

/* Layer 1: Probe - Initialize I2C device */
static int tmp117_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    struct tmp117_data *data;
    s32 device_id;
    int ret;
    
    dev_info(&client->dev, "Probing TMP117 at I2C addr 0x%02x\n", client->addr);
    
    /* Verify hardware by reading device ID */
    device_id = i2c_smbus_read_word_swapped(client, TMP117_REG_DEVICE_ID);
    if (device_id < 0)
        return device_id;
    
    if (device_id != 0x0117) {
        dev_err(&client->dev, "Invalid device ID: 0x%04x\n", device_id);
        return -ENODEV;
    }
    
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    data->client = client;
    i2c_set_clientdata(client, data);
    
    /* Configure hardware */
    ret = tmp117_configure(data);
    if (ret)
        return ret;
    
    /* Initial temperature read */
    ret = tmp117_read_temperature(data);
    if (ret)
        return ret;
    
    dev_info(&client->dev, "TMP117 initialized (temp=%d.%03d°C)\n",
             data->last_temp_mc / 1000, abs(data->last_temp_mc % 1000));
    
    return 0;
}

/* Layer 1: Remove */
static void tmp117_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "TMP117 removed\n");
}

/* Layer 1: I2C driver registration */
static const struct of_device_id tmp117_dt_ids[] = {
    { .compatible = "ti,tmp117" },
    { }
};
MODULE_DEVICE_TABLE(of, tmp117_dt_ids);

static const struct i2c_device_id tmp117_id[] = {
    { "tmp117", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, tmp117_id);

static struct i2c_driver tmp117_driver = {
    .driver = {
        .name = "tmp117",
        .of_match_table = tmp117_dt_ids,
    },
    .probe = tmp117_probe,
    .remove = tmp117_remove,
    .id_table = tmp117_id,
    /* ❌ NO .fops here! */
};

module_i2c_driver(tmp117_driver);
```

**What Layer 1 Does:**
- ✅ Binds to I2C device (address 0x48)
- ✅ Verifies hardware presence
- ✅ Configures device via I2C writes
- ✅ Reads temperature via I2C reads
- ✅ Stores data in kernel memory
- ❌ Does NOT provide userspace access yet!

#### 3. SPI Drivers

For devices on the SPI bus.

**Structure:**

```c
struct spi_driver {
    const struct spi_device_id *id_table;
    int (*probe)(struct spi_device *spi);
    void (*remove)(struct spi_device *spi);
    void (*shutdown)(struct spi_device *spi);
    struct device_driver driver;
};
```

**No `.fops` field!** ❌

**Example: ADC (MCP3008)**

```c
#include <linux/spi/spi.h>
#include <linux/module.h>

struct mcp3008_data {
    struct spi_device *spi;
    u16 last_reading[8];  /* Last reading for each channel */
};

/* Layer 1: Read ADC channel via SPI */
static int mcp3008_read_channel(struct mcp3008_data *data, int channel)
{
    u8 tx[3], rx[3];
    int ret;
    u16 raw;
    
    if (channel < 0 || channel > 7)
        return -EINVAL;
    
    /* MCP3008 SPI protocol:
     * Send 3 bytes: [0x01] [0x80 | (ch << 4)] [0x00]
     * Receive 3 bytes: [xx] [null|b9|b8|b7|b6|b5|b4|b3] [b2|b1|b0|x|x|x|x|x]
     */
    tx[0] = 0x01;  /* Start bit */
    tx[1] = (0x08 | channel) << 4;  /* Single-ended mode + channel */
    tx[2] = 0x00;
    
    /* Perform SPI transaction */
    ret = spi_write_then_read(data->spi, tx, sizeof(tx), rx, sizeof(rx));
    if (ret < 0)
        return ret;
    
    /* Extract 10-bit result */
    raw = ((rx[1] & 0x03) << 8) | rx[2];
    data->last_reading[channel] = raw;
    
    return raw;  /* 0-1023 */
}

/* Layer 1: Test SPI communication */
static int mcp3008_test_communication(struct mcp3008_data *data)
{
    int i, ret;
    
    /* Read all channels to verify SPI works */
    for (i = 0; i < 8; i++) {
        ret = mcp3008_read_channel(data, i);
        if (ret < 0) {
            dev_err(&data->spi->dev, "Failed to read channel %d\n", i);
            return ret;
        }
    }
    
    return 0;
}

/* Layer 1: Probe - Initialize SPI device */
static int mcp3008_probe(struct spi_device *spi)
{
    struct mcp3008_data *data;
    int ret;
    
    dev_info(&spi->dev, "Probing MCP3008 (CS=%d, max_freq=%dHz)\n",
             spi->chip_select, spi->max_speed_hz);
    
    data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    data->spi = spi;
    spi_set_drvdata(spi, data);
    
    /* Configure SPI mode if needed */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    ret = spi_setup(spi);
    if (ret < 0)
        return ret;
    
    /* Test communication */
    ret = mcp3008_test_communication(data);
    if (ret)
        return ret;
    
    dev_info(&spi->dev, "MCP3008 initialized (8 channels, 10-bit)\n");
    return 0;
}

/* Layer 1: Remove */
static void mcp3008_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "MCP3008 removed\n");
}

/* Layer 1: SPI driver registration */
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
    /* ❌ NO .fops here! */
};

module_spi_driver(mcp3008_driver);
```

**What Layer 1 Does:**
- ✅ Binds to SPI device (chip select 0)
- ✅ Configures SPI mode and timing
- ✅ Performs SPI transactions
- ✅ Reads ADC channels
- ✅ Stores readings in kernel memory
- ❌ Does NOT provide userspace access yet!

---

## Layer 2: Userspace Interfaces

### Purpose

Layer 2 provides **userspace access** to the hardware managed by Layer 1:
- File operations (`open`, `read`, `write`, `ioctl`)
- Data formatting for userspace
- Access control and permissions
- Blocking/non-blocking I/O
- Concurrency management

### Common Layer 2 Interface Types

#### 1. Character Device Interface

**Most flexible**, full control over userspace API.

**Structure:**

```c
struct file_operations {
    struct module *owner;
    loff_t (*llseek) (struct file *, loff_t, int);
    ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
    ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
    int (*open) (struct inode *, struct file *);
    int (*release) (struct inode *, struct file *);
    long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
    unsigned int (*poll) (struct file *, struct poll_table_struct *);
    /* ... many more ... */
};
```

**Example: Character Device for Button**

```c
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

/* Assume Layer 1 button_data already exists */

/* Layer 2: Character device state */
struct button_chardev {
    struct cdev cdev;
    dev_t devno;
    struct class *class;
    struct device *device;
    wait_queue_head_t wait_queue;
    char buffer[256];
    int buffer_filled;
    spinlock_t lock;
};

/* Link back to Layer 1 data */
struct button_full_data {
    struct button_data hw;          /* Layer 1: hardware */
    struct button_chardev chardev;  /* Layer 2: userspace interface */
};

/* Layer 2: Open file operation */
static int button_chardev_open(struct inode *inode, struct file *file)
{
    struct button_chardev *chardev;
    
    chardev = container_of(inode->i_cdev, struct button_chardev, cdev);
    file->private_data = chardev;
    
    pr_info("Button device opened\n");
    return 0;
}

/* Layer 2: Read file operation (blocking) */
static ssize_t button_chardev_read(struct file *file, char __user *buf,
                                    size_t count, loff_t *ppos)
{
    struct button_chardev *chardev = file->private_data;
    int ret;
    char local_buf[256];
    size_t len;
    
    /* Wait for button press event */
    ret = wait_event_interruptible(chardev->wait_queue, chardev->buffer_filled);
    if (ret)
        return -ERESTARTSYS;
    
    /* Copy data while holding lock */
    spin_lock_irq(&chardev->lock);
    strscpy(local_buf, chardev->buffer, sizeof(local_buf));
    len = strlen(local_buf);
    chardev->buffer_filled = 0;
    spin_unlock_irq(&chardev->lock);
    
    /* Copy to userspace */
    if (copy_to_user(buf, local_buf, len))
        return -EFAULT;
    
    return len;
}

/* Layer 2: Release file operation */
static int button_chardev_release(struct inode *inode, struct file *file)
{
    pr_info("Button device closed\n");
    return 0;
}

/* Layer 2: File operations structure */
static const struct file_operations button_fops = {
    .owner = THIS_MODULE,
    .open = button_chardev_open,
    .read = button_chardev_read,
    .release = button_chardev_release,
};

/* Layer 2: Push event from Layer 1 (IRQ handler) */
static void button_chardev_push_event(struct button_chardev *chardev,
                                      unsigned long press_count)
{
    spin_lock(&chardev->lock);
    snprintf(chardev->buffer, sizeof(chardev->buffer),
             "Button pressed! Count: %lu\n", press_count);
    chardev->buffer_filled = 1;
    spin_unlock(&chardev->lock);
    
    /* Wake up waiting readers */
    wake_up_interruptible(&chardev->wait_queue);
}

/* Layer 2: Register character device */
static int button_chardev_register(struct button_full_data *full_data,
                                    struct device *parent)
{
    struct button_chardev *chardev = &full_data->chardev;
    int ret;
    
    /* Allocate device number */
    ret = alloc_chrdev_region(&chardev->devno, 0, 1, "my-button");
    if (ret < 0)
        return ret;
    
    /* Initialize cdev with file operations */
    cdev_init(&chardev->cdev, &button_fops);
    chardev->cdev.owner = THIS_MODULE;
    
    /* Add character device to kernel */
    ret = cdev_add(&chardev->cdev, chardev->devno, 1);
    if (ret)
        goto err_chrdev;
    
    /* Create device class */
    chardev->class = class_create(THIS_MODULE, "my-button");
    if (IS_ERR(chardev->class)) {
        ret = PTR_ERR(chardev->class);
        goto err_cdev;
    }
    
    /* Create device node /dev/my-button */
    chardev->device = device_create(chardev->class, parent, chardev->devno,
                                     NULL, "my-button");
    if (IS_ERR(chardev->device)) {
        ret = PTR_ERR(chardev->device);
        goto err_class;
    }
    
    /* Initialize wait queue and spinlock */
    init_waitqueue_head(&chardev->wait_queue);
    spin_lock_init(&chardev->lock);
    
    dev_info(parent, "Character device /dev/my-button created\n");
    return 0;

err_class:
    class_destroy(chardev->class);
err_cdev:
    cdev_del(&chardev->cdev);
err_chrdev:
    unregister_chrdev_region(chardev->devno, 1);
    return ret;
}

/* Layer 2: Unregister character device */
static void button_chardev_unregister(struct button_chardev *chardev)
{
    device_destroy(chardev->class, chardev->devno);
    class_destroy(chardev->class);
    cdev_del(&chardev->cdev);
    unregister_chrdev_region(chardev->devno, 1);
}

/* Modified Layer 1: Probe now calls Layer 2 registration */
static int button_probe(struct platform_device *pdev)
{
    struct button_full_data *full_data;
    int ret;
    
    full_data = devm_kzalloc(&pdev->dev, sizeof(*full_data), GFP_KERNEL);
    if (!full_data)
        return -ENOMEM;
    
    /* Layer 1: Initialize hardware (GPIO, IRQ) */
    full_data->hw.gpiod = devm_gpiod_get(&pdev->dev, NULL, GPIOD_IN);
    if (IS_ERR(full_data->hw.gpiod))
        return PTR_ERR(full_data->hw.gpiod);
    
    full_data->hw.irq = gpiod_to_irq(full_data->hw.gpiod);
    ret = devm_request_irq(&pdev->dev, full_data->hw.irq, button_irq_handler,
                           IRQF_TRIGGER_FALLING, "button", full_data);
    if (ret)
        return ret;
    
    /* Layer 2: Create userspace interface */
    ret = button_chardev_register(full_data, &pdev->dev);
    if (ret)
        return ret;
    
    platform_set_drvdata(pdev, full_data);
    
    dev_info(&pdev->dev, "Button driver ready (hardware + /dev interface)\n");
    return 0;
}

/* Modified Layer 1: Remove now calls Layer 2 cleanup */
static int button_remove(struct platform_device *pdev)
{
    struct button_full_data *full_data = platform_get_drvdata(pdev);
    
    /* Layer 2: Remove userspace interface */
    button_chardev_unregister(&full_data->chardev);
    
    /* Layer 1: Cleanup happens automatically with devm_* */
    
    dev_info(&pdev->dev, "Button driver removed\n");
    return 0;
}
```

**Usage from Userspace:**

```bash
# Blocking read - waits for button press
cat /dev/my-button
# Output: Button pressed! Count: 1

# Or in C program:
int fd = open("/dev/my-button", O_RDONLY);
char buf[256];
read(fd, buf, sizeof(buf));  // Blocks until button pressed
printf("%s", buf);
close(fd);
```

#### 2. Sysfs Attributes

**Simpler** than character device, good for simple read/write values.

**Example: Sysfs for Button**

```c
#include <linux/sysfs.h>

/* Layer 2: Sysfs show function */
static ssize_t press_count_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct button_full_data *full_data = dev_get_drvdata(dev);
    
    /* Read from Layer 1 data */
    return sprintf(buf, "%lu\n", full_data->hw.press_count);
}

/* Layer 2: Sysfs store function */
static ssize_t press_count_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
    struct button_full_data *full_data = dev_get_drvdata(dev);
    unsigned long val;
    int ret;
    
    ret = kstrtoul(buf, 10, &val);
    if (ret)
        return ret;
    
    /* Write to Layer 1 data */
    full_data->hw.press_count = val;
    
    return count;
}

/* Define sysfs attribute */
static DEVICE_ATTR_RW(press_count);

/* Layer 2: Register sysfs attribute */
static int button_sysfs_register(struct device *dev)
{
    return device_create_file(dev, &dev_attr_press_count);
}

/* Layer 2: Unregister sysfs attribute */
static void button_sysfs_unregister(struct device *dev)
{
    device_remove_file(dev, &dev_attr_press_count);
}

/* Modified probe: Add sysfs interface */
static int button_probe(struct platform_device *pdev)
{
    /* ... Layer 1 setup ... */
    
    /* Layer 2: Create sysfs interface */
    ret = button_sysfs_register(&pdev->dev);
    if (ret)
        return ret;
    
    /* ... */
}
```

**Usage from Userspace:**

```bash
# Read press count
cat /sys/bus/platform/devices/my-button/press_count
# Output: 42

# Reset press count
echo 0 > /sys/bus/platform/devices/my-button/press_count
```

#### 3. Multiple Interfaces (Best of Both Worlds!)

You can provide **multiple Layer 2 interfaces** for the same Layer 1 driver!

**Example: Button with THREE interfaces**

```c
/* Layer 1: Hardware data (shared by all interfaces) */
struct button_hw {
    struct gpio_desc *gpiod;
    int irq;
    unsigned long press_count;
};

/* Layer 2a: Character device interface */
struct button_chardev {
    struct cdev cdev;
    /* ... */
};

/* Layer 2b: Input subsystem interface */
struct input_dev *input;

/* Layer 2c: Sysfs interface */
/* (using DEVICE_ATTR macros) */

/* Combined structure */
struct button_full_data {
    struct button_hw hw;              /* Layer 1 */
    struct button_chardev chardev;    /* Layer 2a */
    struct input_dev *input;          /* Layer 2b */
    /* Sysfs uses device attributes */ /* Layer 2c */
};

static int button_probe(struct platform_device *pdev)
{
    struct button_full_data *data;
    
    /* ... */
    
    /* Layer 1: Hardware setup */
    /* ... GPIO, IRQ ... */
    
    /* Layer 2a: Character device */
    button_chardev_register(data, &pdev->dev);
    
    /* Layer 2b: Input device */
    data->input = devm_input_allocate_device(&pdev->dev);
    input_set_capability(data->input, EV_KEY, KEY_ENTER);
    input_register_device(data->input);
    
    /* Layer 2c: Sysfs */
    device_create_file(&pdev->dev, &dev_attr_press_count);
    
    return 0;
}
```

**Now users can access via:**
- `/dev/my-button` (character device)
- `/dev/input/event0` (input subsystem)
- `/sys/.../press_count` (sysfs)

**All three read from the same Layer 1 hardware!**

---

## Complete Examples

### Example 1: I2C Temperature Sensor with Character Device

Full implementation showing both layers.

```c
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define TMP117_REG_TEMP 0x00

/* Layer 1: Hardware state */
struct tmp117_hw {
    struct i2c_client *client;
    int last_temp_mc;
};

/* Layer 2: Character device state */
struct tmp117_chardev {
    struct cdev cdev;
    dev_t devno;
    struct class *class;
    struct device *device;
};

/* Combined driver data */
struct tmp117_data {
    struct tmp117_hw hw;
    struct tmp117_chardev chardev;
};

/* Layer 1: Read temperature from hardware */
static int tmp117_hw_read_temp(struct tmp117_hw *hw)
{
    s32 ret;
    s16 raw;
    
    ret = i2c_smbus_read_word_swapped(hw->client, TMP117_REG_TEMP);
    if (ret < 0)
        return ret;
    
    raw = (s16)ret;
    hw->last_temp_mc = ((long)raw * 78125) / 1000;
    
    return 0;
}

/* Layer 2: Open operation */
static int tmp117_chardev_open(struct inode *inode, struct file *file)
{
    struct tmp117_chardev *chardev;
    struct tmp117_data *data;
    
    chardev = container_of(inode->i_cdev, struct tmp117_chardev, cdev);
    data = container_of(chardev, struct tmp117_data, chardev);
    
    file->private_data = data;
    return 0;
}

/* Layer 2: Read operation */
static ssize_t tmp117_chardev_read(struct file *file, char __user *buf,
                                    size_t count, loff_t *ppos)
{
    struct tmp117_data *data = file->private_data;
    char temp_str[32];
    int ret, len;
    
    /* Call Layer 1 to read hardware */
    ret = tmp117_hw_read_temp(&data->hw);
    if (ret)
        return ret;
    
    /* Format for userspace */
    len = snprintf(temp_str, sizeof(temp_str), "%d.%03d\n",
                   data->hw.last_temp_mc / 1000,
                   abs(data->hw.last_temp_mc % 1000));
    
    return simple_read_from_buffer(buf, count, ppos, temp_str, len);
}

/* Layer 2: File operations */
static const struct file_operations tmp117_fops = {
    .owner = THIS_MODULE,
    .open = tmp117_chardev_open,
    .read = tmp117_chardev_read,
    .llseek = no_llseek,
};

/* Layer 2: Register character device */
static int tmp117_chardev_register(struct tmp117_data *data)
{
    struct tmp117_chardev *chardev = &data->chardev;
    int ret;
    
    ret = alloc_chrdev_region(&chardev->devno, 0, 1, "tmp117");
    if (ret < 0)
        return ret;
    
    cdev_init(&chardev->cdev, &tmp117_fops);
    chardev->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&chardev->cdev, chardev->devno, 1);
    if (ret)
        goto err_chrdev;
    
    chardev->class = class_create(THIS_MODULE, "tmp117");
    if (IS_ERR(chardev->class)) {
        ret = PTR_ERR(chardev->class);
        goto err_cdev;
    }
    
    chardev->device = device_create(chardev->class, &data->hw.client->dev,
                                     chardev->devno, NULL, "tmp117");
    if (IS_ERR(chardev->device)) {
        ret = PTR_ERR(chardev->device);
        goto err_class;
    }
    
    return 0;

err_class:
    class_destroy(chardev->class);
err_cdev:
    cdev_del(&chardev->cdev);
err_chrdev:
    unregister_chrdev_region(chardev->devno, 1);
    return ret;
}

/* Layer 2: Unregister character device */
static void tmp117_chardev_unregister(struct tmp117_chardev *chardev)
{
    device_destroy(chardev->class, chardev->devno);
    class_destroy(chardev->class);
    cdev_del(&chardev->cdev);
    unregister_chrdev_region(chardev->devno, 1);
}

/* Layer 1: I2C probe */
static int tmp117_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    struct tmp117_data *data;
    int ret;
    
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    /* Layer 1: Initialize hardware */
    data->hw.client = client;
    i2c_set_clientdata(client, data);
    
    ret = tmp117_hw_read_temp(&data->hw);
    if (ret)
        return ret;
    
    /* Layer 2: Create userspace interface */
    ret = tmp117_chardev_register(data);
    if (ret)
        return ret;
    
    dev_info(&client->dev, "TMP117 ready: hardware initialized, /dev/tmp117 created\n");
    return 0;
}

/* Layer 1: I2C remove */
static void tmp117_remove(struct i2c_client *client)
{
    struct tmp117_data *data = i2c_get_clientdata(client);
    
    /* Layer 2: Remove userspace interface */
    tmp117_chardev_unregister(&data->chardev);
    
    /* Layer 1: Hardware cleanup (automatic with devm_*) */
    
    dev_info(&client->dev, "TMP117 removed\n");
}

/* Layer 1: I2C driver registration */
static const struct of_device_id tmp117_dt_ids[] = {
    { .compatible = "ti,tmp117" },
    { }
};
MODULE_DEVICE_TABLE(of, tmp117_dt_ids);

static const struct i2c_device_id tmp117_id[] = {
    { "tmp117", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, tmp117_id);

static struct i2c_driver tmp117_driver = {
    .driver = {
        .name = "tmp117",
        .of_match_table = tmp117_dt_ids,
    },
    .probe = tmp117_probe,
    .remove = tmp117_remove,
    .id_table = tmp117_id,
};

module_i2c_driver(tmp117_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("TMP117 Temperature Sensor with Character Device");
```

**Testing:**

```bash
# Layer 1 works (I2C communication)
i2cget -y 2 0x48 0x00 w
# Output: 0x1900 (raw value)

# Layer 2 works (character device)
cat /dev/tmp117
# Output: 25.123 (formatted for humans)

# Both access the same hardware!
```

### Example 2: SPI ADC with IIO Subsystem

Using kernel subsystem instead of custom chardev.

```c
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

/* Layer 1: Hardware state */
struct mcp3008_hw {
    struct spi_device *spi;
};

/* Combined with IIO */
struct mcp3008_data {
    struct mcp3008_hw hw;
    /* IIO handles Layer 2 for us! */
};

/* Layer 1: Read ADC channel via SPI */
static int mcp3008_hw_read_channel(struct mcp3008_hw *hw, int channel)
{
    u8 tx[3], rx[3];
    int ret;
    
    tx[0] = 0x01;
    tx[1] = (0x08 | channel) << 4;
    tx[2] = 0x00;
    
    ret = spi_write_then_read(hw->spi, tx, 3, rx, 3);
    if (ret < 0)
        return ret;
    
    return ((rx[1] & 0x03) << 8) | rx[2];
}

/* Layer 2: IIO read_raw callback (called when userspace reads) */
static int mcp3008_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct mcp3008_data *data = iio_priv(indio_dev);
    int ret;
    
    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        /* Call Layer 1 to read hardware */
        ret = mcp3008_hw_read_channel(&data->hw, chan->channel);
        if (ret < 0)
            return ret;
        
        *val = ret;
        return IIO_VAL_INT;
        
    case IIO_CHAN_INFO_SCALE:
        /* 3.3V / 1024 = 0.003225806 V/LSB */
        *val = 3300;  /* mV */
        *val2 = 10;   /* 2^10 = 1024 */
        return IIO_VAL_FRACTIONAL_LOG2;
        
    default:
        return -EINVAL;
    }
}

/* Layer 2: IIO info structure */
static const struct iio_info mcp3008_info = {
    .read_raw = mcp3008_read_raw,
};

/* Define 8 IIO channels */
#define MCP3008_CHANNEL(ch) { \
    .type = IIO_VOLTAGE, \
    .indexed = 1, \
    .channel = ch, \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
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

/* Layer 1: SPI probe */
static int mcp3008_probe(struct spi_device *spi)
{
    struct iio_dev *indio_dev;
    struct mcp3008_data *data;
    int ret;
    
    /* Allocate IIO device (includes our data) */
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;
    
    data = iio_priv(indio_dev);
    
    /* Layer 1: Initialize hardware */
    data->hw.spi = spi;
    spi_set_drvdata(spi, indio_dev);
    
    /* Layer 2: Configure IIO device */
    indio_dev->name = "mcp3008";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = mcp3008_channels;
    indio_dev->num_channels = ARRAY_SIZE(mcp3008_channels);
    indio_dev->info = &mcp3008_info;
    
    /* Layer 2: Register IIO device (creates /sys/bus/iio/...) */
    ret = devm_iio_device_register(&spi->dev, indio_dev);
    if (ret)
        return ret;
    
    dev_info(&spi->dev, "MCP3008 ready: SPI comm OK, IIO device registered\n");
    return 0;
}

/* Layer 1: SPI driver */
static const struct of_device_id mcp3008_dt_ids[] = {
    { .compatible = "microchip,mcp3008" },
    { }
};
MODULE_DEVICE_TABLE(of, mcp3008_dt_ids);

static struct spi_driver mcp3008_driver = {
    .driver = {
        .name = "mcp3008",
        .of_match_table = mcp3008_dt_ids,
    },
    .probe = mcp3008_probe,
};

module_spi_driver(mcp3008_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MCP3008 ADC with IIO Subsystem");
```

**Testing:**

```bash
# Layer 2: IIO subsystem provides standard interface
ls /sys/bus/iio/devices/iio:device0/
# Output: in_voltage0_raw  in_voltage1_raw  ...  in_voltage_scale

# Read channel 0
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
# Output: 512

# Read scale
cat /sys/bus/iio/devices/iio:device0/in_voltage_scale
# Output: 0.003225806

# Calculate voltage: 512 * 0.003225806 = 1.652V
```

**No custom character device needed!** IIO subsystem handles Layer 2 for us.

---

## Kernel Subsystems (The Smart Alternative)

Instead of creating custom character devices, use **existing kernel subsystems** for Layer 2.

### Comparison

| Approach | Complexity | Flexibility | Standardization |
|----------|------------|-------------|-----------------|
| **Custom Character Device** | High | Maximum | None (custom API) |
| **Kernel Subsystem** | Low | Limited | High (standard API) |

### Available Subsystems

#### 1. IIO (Industrial I/O)

**Best for:** ADCs, DACs, sensors, accelerometers, gyroscopes

**Layer 2 provided by:** IIO core

**Userspace interface:** `/sys/bus/iio/devices/iio:deviceX/`

**Example devices:**
- MCP3008 ADC
- BME280 pressure/temp/humidity sensor
- MPU6050 accelerometer/gyroscope

**Why use it:**
- ✅ Standard API for all IIO devices
- ✅ Tools like `iio_info` work automatically
- ✅ Buffered reads for high-speed sampling
- ✅ Trigger support for synchronized capture
- ✅ Userspace libraries available

#### 2. Hwmon (Hardware Monitoring)

**Best for:** Temperature sensors, fan controllers, voltage monitors

**Layer 2 provided by:** Hwmon core

**Userspace interface:** `/sys/class/hwmon/hwmonX/`

**Example devices:**
- TMP117 temperature sensor
- LM75 temperature sensor
- INA219 current/voltage monitor

**Why use it:**
- ✅ Standard API (`temp1_input`, `temp1_max`, etc.)
- ✅ Tools like `sensors` work automatically
- ✅ systemd can monitor values
- ✅ Automatic unit conversion (millidegrees)

**Example:**

```c
#include <linux/hwmon.h>

/* Layer 2: Hwmon read callback */
static int tmp117_hwmon_read(struct device *dev,
                             enum hwmon_sensor_types type,
                             u32 attr, int channel, long *val)
{
    struct tmp117_data *data = dev_get_drvdata(dev);
    
    if (type != hwmon_temp || attr != hwmon_temp_input)
        return -EOPNOTSUPP;
    
    /* Call Layer 1 */
    return tmp117_hw_read_temp(&data->hw, val);
}

static const struct hwmon_ops tmp117_hwmon_ops = {
    .read = tmp117_hwmon_read,
};

/* In probe: */
data->hwmon_dev = devm_hwmon_device_register_with_info(
    &client->dev, "tmp117", data, HWMON_C_TEMP, NULL, &tmp117_hwmon_ops);
```

**Usage:**

```bash
sensors
# Output:
# tmp117-i2c-2-48
# Adapter: BeagleBone I2C 2
# temp1:        +25.1°C
```

#### 3. Input Subsystem

**Best for:** Buttons, keyboards, touchscreens, joysticks

**Layer 2 provided by:** Input core

**Userspace interface:** `/dev/input/eventX`

**Example devices:**
- GPIO buttons
- Matrix keyboards
- Touchscreens
- Game controllers

**Why use it:**
- ✅ Standard event API (all input devices same)
- ✅ Tools like `evtest` work automatically
- ✅ Xorg/Wayland recognize input automatically
- ✅ Key repeat and debouncing built-in

**Example:**

```c
#include <linux/input.h>

/* In probe: */
input = devm_input_allocate_device(&pdev->dev);
input->name = "My Button";
input_set_capability(input, EV_KEY, KEY_ENTER);
input_register_device(input);

/* In IRQ handler (Layer 1): */
input_report_key(input, KEY_ENTER, 1);  /* Pressed */
input_sync(input);
input_report_key(input, KEY_ENTER, 0);  /* Released */
input_sync(input);
```

**Usage:**

```bash
evtest /dev/input/event0
# Output: Press KEY_ENTER when button pressed
```

#### 4. Other Subsystems

| Subsystem | Use Case | Interface |
|-----------|----------|-----------|
| **LED** | LED control | `/sys/class/leds/` |
| **PWM** | PWM output | `/sys/class/pwm/` |
| **GPIO** | GPIO lines | `/dev/gpiochipX` |
| **RTC** | Real-time clocks | `/dev/rtcX` |
| **Watchdog** | Watchdog timers | `/dev/watchdogX` |
| **Regulator** | Voltage/current regulators | `/sys/class/regulator/` |

---

## When to Use What

### Decision Tree

```bash
Start: Need userspace access to hardware
  │
  ├─► Is there a standard subsystem for this device type?
  │     │
  │     ├─► YES: Use the subsystem! (IIO, hwmon, input, etc.)
  │     │         ✅ Less code, standard API, better maintainability
  │     │
  │     └─► NO: Continue...
  │
  ├─► Need complex operations (ioctl, seeking, etc.)?
  │     │
  │     ├─► YES: Character device
  │     │
  │     └─► NO: Continue...
  │
  ├─► Just reading/writing simple values?
  │     │
  │     └─► Sysfs attributes (simplest!)
  │
  └─► Need streaming/blocking/events?
        │
        └─► Character device with poll/wait_queue
```

### Recommendations

#### Use **Kernel Subsystems** when:
- ✅ Standard device type (sensor, ADC, button, LED, etc.)
- ✅ Want compatibility with existing tools
- ✅ Need less code and maintenance
- ✅ Standard API is sufficient

**Examples:**
- Temperature sensor → **Hwmon**
- ADC → **IIO**
- Button → **Input subsystem**
- LED → **LED subsystem**

#### Use **Sysfs** when:
- ✅ Simple read/write of parameters
- ✅ Configuration values
- ✅ Statistics and counters
- ✅ No subsystem available

**Examples:**
- Press counter
- Debounce interval setting
- Driver statistics

#### Use **Character Device** when:
- ✅ Custom protocol needed
- ✅ Complex ioctl operations
- ✅ Streaming data
- ✅ Blocking/non-blocking I/O with custom logic
- ✅ No appropriate subsystem exists

**Examples:**
- Custom sensor with proprietary protocol
- Device with complex state machine
- Legacy API compatibility

#### Use **Multiple Interfaces** when:
- ✅ Different use cases need different interfaces
- ✅ Want flexibility for users
- ✅ Can maintain the code

**Examples:**
- Button: Input (for GUI) + Sysfs (for statistics) + Chardev (for custom events)
- Sensor: Hwmon (for monitoring) + Chardev (for calibration)

---

## Advanced Patterns

### Pattern 1: Shared Data Between Layers

```c
struct my_device {
    /* Layer 1: Hardware state */
    struct {
        struct i2c_client *client;
        int raw_value;
        struct mutex hw_lock;  /* Protects hardware access */
    } hw;
    
    /* Layer 2a: Character device */
    struct {
        struct cdev cdev;
        dev_t devno;
        wait_queue_head_t wait;
        char event_buffer[256];
        spinlock_t buffer_lock;  /* Protects buffer */
    } chardev;
    
    /* Layer 2b: Sysfs (just use device attributes) */
    
    /* Layer 2c: IIO */
    /* IIO device is parent struct, we're embedded in it */
};

/* Layer 1 reads hardware */
static int my_device_hw_read(struct my_device *dev)
{
    int ret;
    
    mutex_lock(&dev->hw.hw_lock);
    ret = i2c_smbus_read_word_swapped(dev->hw.client, REG_DATA);
    if (ret >= 0)
        dev->hw.raw_value = ret;
    mutex_unlock(&dev->hw.hw_lock);
    
    return ret;
}

/* Layer 2a reads from Layer 1 cache */
static ssize_t chardev_read(struct file *file, char __user *buf,
                            size_t count, loff_t *ppos)
{
    struct my_device *dev = file->private_data;
    char temp[32];
    int len;
    
    /* Read cached value (no hardware access!) */
    len = snprintf(temp, sizeof(temp), "%d\n", dev->hw.raw_value);
    
    return simple_read_from_buffer(buf, count, ppos, temp, len);
}

/* Layer 2b reads from Layer 1 cache */
static ssize_t raw_value_show(struct device *device,
                               struct device_attribute *attr, char *buf)
{
    struct my_device *dev = dev_get_drvdata(device);
    
    /* Read cached value */
    return sprintf(buf, "%d\n", dev->hw.raw_value);
}
```

**Key point:** Hardware read happens once in Layer 1, multiple Layer 2 interfaces can access the cached value.

### Pattern 2: Event Notification

Layer 1 (IRQ handler) notifies multiple Layer 2 interfaces:

```c
/* Layer 1: IRQ handler */
static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
    struct my_device *dev = dev_id;
    int value;
    
    /* Read hardware */
    value = gpiod_get_value(dev->hw.gpiod);
    
    /* Notify Layer 2a: Character device (wake up readers) */
    spin_lock(&dev->chardev.buffer_lock);
    snprintf(dev->chardev.event_buffer, sizeof(dev->chardev.event_buffer),
             "Event! Value=%d\n", value);
    spin_unlock(&dev->chardev.buffer_lock);
    wake_up_interruptible(&dev->chardev.wait);
    
    /* Notify Layer 2b: Input subsystem */
    input_report_key(dev->input, KEY_ENTER, value);
    input_sync(dev->input);
    
    /* Layer 2c: Sysfs auto-updates when read */
    dev->hw.last_value = value;
    
    return IRQ_HANDLED;
}
```

### Pattern 3: Lazy Initialization

Only create Layer 2 interfaces if configured:

```c
/* Module parameters to enable/disable interfaces */
static bool enable_chardev = true;
module_param(enable_chardev, bool, 0644);

static bool enable_input = true;
module_param(enable_input, bool, 0644);

static int my_probe(struct platform_device *pdev)
{
    /* Layer 1: Always initialize hardware */
    /* ... */
    
    /* Layer 2a: Conditionally create character device */
    if (enable_chardev) {
        ret = my_chardev_register(data);
        if (ret)
            dev_warn(&pdev->dev, "Failed to create chardev\n");
    }
    
    /* Layer 2b: Conditionally create input device */
    if (enable_input) {
        ret = my_input_register(data);
        if (ret)
            dev_warn(&pdev->dev, "Failed to create input device\n");
    }
    
    /* Layer 2c: Sysfs always created */
    device_create_file(&pdev->dev, &dev_attr_value);
    
    return 0;
}
```

### Pattern 4: Modular Interface Registration

Separate files for each interface:

```bash
my_driver/
├── my_driver_hw.c      # Layer 1: Hardware operations
├── my_driver_hw.h      # Shared hardware API
├── my_driver_chardev.c # Layer 2a: Character device
├── my_driver_chardev.h
├── my_driver_sysfs.c   # Layer 2b: Sysfs attributes
├── my_driver_sysfs.h
├── my_driver_main.c    # Main driver (probe/remove)
└── Makefile
```

**Makefile:**

```makefile
obj-m := my_driver_combined.o

my_driver_combined-y := my_driver_main.o \
                        my_driver_hw.o \
                        my_driver_chardev.o \
                        my_driver_sysfs.o
```

**Header (my_driver_hw.h):**

```c
struct my_hw_device {
    /* Hardware state */
};

/* Layer 1 API for Layer 2 to use */
int my_hw_read(struct my_hw_device *hw, int *value);
int my_hw_write(struct my_hw_device *hw, int value);
```

**Layer 2a (my_driver_chardev.c):**

```c
#include "my_driver_hw.h"

static ssize_t my_chardev_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos)
{
    struct my_hw_device *hw = file->private_data;
    int value;
    
    /* Use Layer 1 API */
    my_hw_read(hw, &value);
    
    /* Format and return */
    /* ... */
}
```

---

## Real-World Case Studies

### Case Study 1: Your BBB Button Driver

**Perfect example** of multi-layer, multi-interface design!

#### Layer Structure

```bash
Layer 1: bbb_flagship_button.c (platform_driver)
  ├─► GPIO acquisition
  ├─► IRQ handling
  ├─► Debouncing (delayed_work)
  └─► Press counting

Layer 2a: bbb_flagship_button_chardev.c
  ├─► Character device (/dev/bbb-button)
  ├─► Blocking read (wait_queue)
  └─► Event formatting

Layer 2b: Input subsystem (in main driver)
  ├─► Input device (/dev/input/eventX)
  └─► KEY_ENTER events

Layer 2c: Sysfs (in main driver)
  ├─► press_count
  ├─► total_irqs
  └─► work_executions
```

#### Data Flow

```bash
Hardware Button Press
        ↓
Layer 1: button_irq_handler()
        ├─► Schedule delayed_work (debouncing)
        ↓
Layer 1: button_debounce_work()
        ├─► Read GPIO
        ├─► Update press_count
        ├─► Notify Layer 2a (chardev push event)
        ├─► Notify Layer 2b (input_report_key)
        └─► Layer 2c auto-updates (sysfs reads live data)
        ↓
Userspace:
   ├─► cat /dev/bbb-button          (Layer 2a)
   ├─► evtest /dev/input/event0     (Layer 2b)
   └─► cat /sys/.../press_count     (Layer 2c)
```

#### Why This Design is Excellent

1. **Separation:** Hardware logic separate from userspace interfaces
2. **Flexibility:** Users can choose their preferred interface
3. **Maintainability:** Each layer can be updated independently
4. **Reusability:** Chardev helper can be used by other drivers
5. **Professional:** Follows Linux kernel best practices

### Case Study 2: TMP117 Temperature Sensor

**Two approaches** possible:

#### Approach A: Hwmon Subsystem (Recommended)

```c
/* Layer 1: I2C driver */
static int tmp117_probe(struct i2c_client *client, ...)
{
    /* Initialize I2C communication */
    
    /* Layer 2: Register with hwmon subsystem */
    hwmon_dev = devm_hwmon_device_register_with_info(...);
    
    /* Done! Hwmon handles userspace */
}
```

**Benefits:**
- ✅ Standard API
- ✅ Works with `sensors` command
- ✅ Less code
- ✅ Better integration

#### Approach B: Custom Character Device

```c
/* Layer 1: I2C driver */
static int tmp117_probe(struct i2c_client *client, ...)
{
    /* Initialize I2C communication */
    
    /* Layer 2: Register character device */
    tmp117_chardev_register(data);
    
    /* Must implement all userspace interface code */
}
```

**When to use:**
- ❌ Custom format required
- ❌ Additional operations (calibration, etc.)
- ❌ Legacy compatibility

**Verdict:** Use hwmon unless you have specific reasons not to!

### Case Study 3: MCP3008 ADC

**IIO subsystem is perfect** for this:

```c
/* Layer 1: SPI communication */
static int mcp3008_read_channel(struct mcp3008 *adc, int ch)
{
    /* SPI transaction */
}

/* Layer 2: IIO subsystem */
static int mcp3008_read_raw(struct iio_dev *indio_dev, ...)
{
    /* Call Layer 1 */
    return mcp3008_read_channel(adc, chan->channel);
}

/* In probe: */
ret = devm_iio_device_register(&spi->dev, indio_dev);
```

**Benefits:**
- ✅ Standard IIO API
- ✅ Buffered reads for high-speed
- ✅ Trigger support
- ✅ libiio userspace library
- ✅ Works with IIO tools

---

## Best Practices

### DO:

1. **✅ Use kernel subsystems** when possible (IIO, hwmon, input, etc.)
2. **✅ Separate Layer 1 (hardware) from Layer 2 (userspace)**
3. **✅ Use `devm_*` functions** for automatic cleanup
4. **✅ Protect shared data** with appropriate locks
5. **✅ Document your layer boundaries** in comments
6. **✅ Provide multiple interfaces** if it helps users
7. **✅ Keep Layer 1 bus-agnostic** (can swap I2C for SPI)
8. **✅ Use standard error codes** (`-EINVAL`, `-EIO`, etc.)
9. **✅ Log important events** with `dev_info()`, `dev_err()`
10. **✅ Handle errors gracefully** in probe/remove

### DON'T:

1. **❌ Don't mix Layer 1 and Layer 2 code** in same functions
2. **❌ Don't create custom chardev** for standard device types
3. **❌ Don't access hardware directly** from file_operations
4. **❌ Don't forget locking** for shared data
5. **❌ Don't use `copy_to_user()` in IRQ context** (use buffer + wake_up)
6. **❌ Don't leak resources** on error paths (use `devm_*`)
7. **❌ Don't assume probe order** (use proper dependencies)
8. **❌ Don't hardcode values** (use device tree properties)
9. **❌ Don't ignore return values** from kernel functions
10. **❌ Don't add `.fops` to bus driver structs** (they don't have it!)

### Code Organization

**Good structure:**

```c
/*
 * my_driver.c - Example driver showing layer separation
 *
 * Layer 1: Hardware communication (I2C/SPI/Platform)
 * Layer 2: Userspace interface (chardev/sysfs/subsystem)
 */

/* ========== Layer 1: Hardware Operations ========== */

struct my_hw {
    /* Hardware-specific fields */
};

static int my_hw_read(struct my_hw *hw) { ... }
static int my_hw_write(struct my_hw *hw, int val) { ... }
static irqreturn_t my_hw_irq(int irq, void *data) { ... }

/* ========== Layer 2: Userspace Interface ========== */

static int my_chardev_open(struct inode *inode, struct file *file) { ... }
static ssize_t my_chardev_read(struct file *file, ...) {
    /* Call Layer 1 functions */
    my_hw_read(hw);
}

static const struct file_operations my_fops = { ... };

/* ========== Driver Registration ========== */

static int my_probe(struct XXX_device *dev)
{
    /* Layer 1 init */
    my_hw_init(...);
    
    /* Layer 2 init */
    my_chardev_register(...);
}

static int my_remove(struct XXX_device *dev)
{
    /* Layer 2 cleanup */
    my_chardev_unregister(...);
    
    /* Layer 1 cleanup */
    my_hw_cleanup(...);
}
```

---

## Summary

### Key Concepts

1. **Two Layers:**
   - Layer 1: Hardware communication (I2C/SPI/Platform drivers)
   - Layer 2: Userspace interface (chardev/sysfs/subsystems)

2. **No `.fops` in Bus Drivers:**
   - `platform_driver`, `i2c_driver`, `spi_driver` do **NOT** have file_operations
   - Character device is created **separately** in probe()

3. **Kernel Subsystems:**
   - IIO for ADCs/sensors
   - Hwmon for temperature/voltage
   - Input for buttons/keyboards
   - Use them when possible!

4. **Multiple Interfaces:**
   - Same Layer 1 can have multiple Layer 2 interfaces
   - Each serves different use cases
   - Example: Button with chardev + input + sysfs

### Architecture Summary

```
┌─────────────────────────────────────────────────────────┐
│                    Application                          │
│          (reads /dev, /sys, uses tools)                 │
└────────────────────┬───────────────────────────────────┘
                     │ System calls
┌────────────────────┴───────────────────────────────────┐
│              LAYER 2: Userspace API                     │
│                                                          │
│  Character Device │ Sysfs │ IIO │ Hwmon │ Input        │
│  (custom .fops)   │ (attr)│(std)│ (std) │ (std)        │
│                                                          │
│  Created in probe() with cdev_add(), device_create(),   │
│  iio_device_register(), etc.                            │
└────────────────────┬───────────────────────────────────┘
                     │ Internal API
┌────────────────────┴───────────────────────────────────┐
│         LAYER 1: Hardware Communication                 │
│                                                          │
│  platform_driver │ i2c_driver │ spi_driver             │
│  (GPIO, IRQ)     │(i2c_smbus_)│(spi_transfer)          │
│                                                          │
│  Registered with module_XXX_driver()                    │
│  ❌ NO .fops field!                                     │
└────────────────────┬───────────────────────────────────┘
                     │ Register access
┌────────────────────┴───────────────────────────────────┐
│                   Hardware                              │
└─────────────────────────────────────────────────────────┘
```

---

**You now understand the complete Linux driver architecture!** 🎉

Use this guide as reference when designing your drivers. Remember:
- **Layer 1** = Hardware expert
- **Layer 2** = Userspace servant

Keep them separate, and your code will be clean, maintainable, and professional!

---

**Document Status:** Complete reference guide  
**Last Updated:** December 29, 2025  
**Author:** Chun

---


