# Input Subsystem Driver Guide

**Date:** December 27, 2024  
**Goal:** Add Linux input subsystem support to button driver for standard event interface

---

## Overview

This guide shows how to add **input subsystem** support to your existing button driver. After implementation, your button will appear as a standard Linux input device (`/dev/input/eventX`), compatible with all input tools and applications.

### What You'll Build

**Before:** Custom character device only
```bash
cat /dev/bbb-button
# Custom format: "button pressed: count=1 time=12345"
```

**After:** Standard input device + character device
```bash
evtest /dev/input/event0
# Standard format: Event: time 1234.567890, type 1 (EV_KEY), code 28 (KEY_ENTER), value 1
```

### Why Input Subsystem?

**Advantages:**
- ✅ Standard interface (works with `evtest`, `libinput`, X11, Wayland)
- ✅ Auto-integration with GUI frameworks
- ✅ Supports key repeat, LED feedback, force feedback
- ✅ Multi-event delivery (can report multiple simultaneous events)
- ✅ Works with existing input tools (`xinput`, `libinput-debug-events`)

**Use Cases:**
- Keyboards, mice, touchpads
- Game controllers, joysticks
- Touch screens
- Custom buttons (like yours!)
- Rotary encoders, sliders

---

## Architecture: Triple Interface

Your button will have THREE interfaces:

```bash
                    ┌─────────────────────┐
                    │  Platform Driver    │
                    │  (GPIO + IRQ)       │
                    └──────────┬──────────┘
                               │
                 ┌─────────────┼─────────────┐
                 │             │             │
                 ▼             ▼             ▼
        ┌────────────┐  ┌────────────┐  ┌────────────┐
        │   sysfs    │  │  chardev   │  │   input    │
        │            │  │            │  │            │
        │ press_count│  │/dev/bbb-btn│  │/dev/input/ │
        │ total_irqs │  │            │  │  event0    │
        └────────────┘  └────────────┘  └────────────┘
             │               │               │
             ▼               ▼               ▼
         Debugging      Custom apps    Standard apps
```

**All three interfaces work simultaneously!**

---

## Part 1: Understanding Input Events

### Input Event Structure

```c
struct input_event {
    struct timeval time;  // Timestamp
    __u16 type;           // Event type (EV_KEY, EV_REL, EV_ABS, etc.)
    __u16 code;           // Event code (KEY_ENTER, BTN_LEFT, etc.)
    __s32 value;          // Event value (0=release, 1=press, 2=repeat)
};
```

### Event Types

| Type | Value | Description | Example |
|------|-------|-------------|---------|
| `EV_SYN` | 0 | Synchronization | End of event group |
| `EV_KEY` | 1 | Key/button | Keyboard, mouse buttons |
| `EV_REL` | 2 | Relative axis | Mouse movement |
| `EV_ABS` | 3 | Absolute axis | Touchscreen coordinates |
| `EV_MSC` | 4 | Miscellaneous | Scan codes |
| `EV_LED` | 17 | LED | Keyboard LEDs |

**For buttons, we use `EV_KEY`.**

### Key Codes

Common codes in `<linux/input-event-codes.h>`:

```c
KEY_ENTER       28     // Enter key
KEY_ESC         1      // Escape
KEY_SPACE       57     // Space bar
BTN_0           0x100  // Generic button 0
BTN_1           0x101  // Generic button 1
BTN_LEFT        0x110  // Mouse left button
BTN_RIGHT       0x111  // Mouse right button
KEY_VOLUMEUP    115    // Volume up
KEY_VOLUMEDOWN  114    // Volume down
KEY_POWER       116    // Power button
```

**We'll use `KEY_ENTER` for your button.**

### Event Sequence

**Button press:**
```bash
Event: time 1234.567890, type 1 (EV_KEY), code 28 (KEY_ENTER), value 1
Event: time 1234.567890, type 0 (EV_SYN), code 0 (SYN_REPORT), value 0
```

**Button release:**
```bash
Event: time 1235.123456, type 1 (EV_KEY), code 28 (KEY_ENTER), value 0
Event: time 1235.123456, type 0 (EV_SYN), code 0 (SYN_REPORT), value 0
```

`EV_SYN` marks end of event group (allows multiple simultaneous events).

---

## Part 2: Adding Input Device to Your Driver

### Step 1: Include Headers

Add to `bbb_flagship_button.c`:

```c
#include <linux/input.h>
```

### Step 2: Add Input Device to Driver State

Modify `bbb_flagship_button_chardev.h`:

```c
struct bbb_btn {
    // Existing fields
    struct device *dev;
    struct gpio_desc *gpiod;
    int irq;
    atomic64_t press_count;
    // ... other fields ...
    
    // Character device
    struct {
        dev_t devt;
        struct cdev cdev;
        // ... chardev fields ...
    } chardev;
    
    // NEW: Input device
    struct input_dev *input;  // ← ADD THIS
};
```

### Step 3: Register Input Device in probe()

Add to `bbb_btn_probe()` in `bbb_flagship_button.c`:

```c
static int bbb_btn_probe(struct platform_device *pdev)
{
    struct bbb_btn *b;
    int ret;
    
    // ... existing initialization ...
    
    // Register character device (existing)
    ret = bbb_chardev_register(b, &pdev->dev);
    if (ret)
        return dev_err_probe(&pdev->dev, ret, "chardev registration failed\n");
    
    // NEW: Allocate input device
    b->input = devm_input_allocate_device(&pdev->dev);
    if (!b->input) {
        bbb_chardev_unregister(b);
        return -ENOMEM;
    }
    
    // NEW: Configure input device
    b->input->name = "BeagleBone Black Flagship Button";
    b->input->phys = "bbb-flagship-button/input0";
    b->input->id.bustype = BUS_HOST;
    b->input->id.vendor = 0x0001;
    b->input->id.product = 0x0001;
    b->input->id.version = 0x0100;
    
    // NEW: Set event capabilities
    input_set_capability(b->input, EV_KEY, KEY_ENTER);
    
    // NEW: Associate driver data with input device
    input_set_drvdata(b->input, b);
    
    // NEW: Register input device
    ret = input_register_device(b->input);
    if (ret) {
        bbb_chardev_unregister(b);
        return dev_err_probe(&pdev->dev, ret, "input registration failed\n");
    }
    
    dev_info(&pdev->dev, "driver loaded (irq=%d, debounce=%u ms, input=%s)\n",
             b->irq, b->debounce_ms, b->input->name);
    
    return 0;
}
```

**Key points:**
- `devm_input_allocate_device()` - Automatically freed on remove
- `input_set_capability()` - Declares we can send `KEY_ENTER` events
- `input_register_device()` - Creates `/dev/input/eventX`

### Step 4: Send Events from IRQ Handler

Modify `bbb_btn_debounce_work()` in `bbb_flagship_button.c`:

```c
static void bbb_btn_debounce_work(struct work_struct *work)
{
    struct bbb_btn *b = container_of(work, struct bbb_btn, debounce_work.work);
    char msg[256];
    int state;
    unsigned long flags;
    
    // Read current GPIO state
    state = gpiod_get_value(b->gpiod);
    
    spin_lock_irqsave(&b->lock, flags);
    
    // Check if state actually changed (debounce validation)
    if (state == b->last_state) {
        b->work_pending = false;
        spin_unlock_irqrestore(&b->lock, flags);
        return;  // Spurious, ignore
    }
    
    b->last_state = state;
    b->work_pending = false;
    
    spin_unlock_irqrestore(&b->lock, flags);
    
    // Update counters
    atomic64_set(&b->last_event_ns, ktime_get_ns());
    atomic64_inc(&b->press_count);
    atomic64_inc(&b->work_executions);
    
    // NEW: Report to input subsystem
    input_report_key(b->input, KEY_ENTER, !state);  // !state because GPIO_ACTIVE_LOW
    input_sync(b->input);
    
    // Existing: Send to character device
    snprintf(msg, sizeof(msg), "button %s: count=%lld time=%lld\n",
             state ? "released" : "pressed",
             atomic64_read(&b->press_count),
             ktime_get_ns());
    bbb_chardev_push_event(b, msg);
    
    dev_dbg(b->dev, "Button %s (state=%d, count=%lld)\n",
            state ? "released" : "pressed", state,
            atomic64_read(&b->press_count));
}
```

**Key functions:**
- `input_report_key(input, keycode, value)` - Report key state change
  - `value = 1` means pressed
  - `value = 0` means released
- `input_sync(input)` - Sends `EV_SYN` to mark end of event group

### Step 5: Cleanup on Remove

The input device is automatically freed because we used `devm_input_allocate_device()`. No changes needed in `bbb_btn_remove()`!

---

## Part 3: Device Tree Binding (Optional)

You can specify the key code in device tree:

**In DTBO:**
```c
bbb_flagship_button: button {
    compatible = "bbb,flagship-button";
    button-gpios = <&gpio2 2 GPIO_ACTIVE_LOW>;
    debounce-ms = <20>;
    linux,code = <KEY_ENTER>;  /* NEW: Key code from dt-bindings/input/input-event-codes.h */
};
```

**In driver probe():**
```c
u32 keycode = KEY_ENTER;  // Default

// Read from device tree if specified
of_property_read_u32(pdev->dev.of_node, "linux,code", &keycode);

// Set capability
input_set_capability(b->input, EV_KEY, keycode);
```

This allows configuring different key codes without recompiling!

---

## Part 4: Testing the Input Device

### Test 1: Find Your Input Device

```bash
# List all input devices
ls -l /dev/input/event*

# Find your device by name
cat /proc/bus/input/devices

# Output:
# I: Bus=0019 Vendor=0001 Product=0001 Version=0100
# N: Name="BeagleBone Black Flagship Button"
# P: Phys=bbb-flagship-button/input0
# S: Sysfs=/devices/platform/bbb-flagship-button/input/input0/event0
# U: Uniq=
# H: Handlers=event0 
# B: PROP=0
# B: EV=3
# B: KEY=10000000 0 0 0
```

**Your device is `/dev/input/event0`!**

### Test 2: Monitor Events with evtest

```bash
# Install evtest (if not installed)
apt-get install evtest

# Monitor events
evtest /dev/input/event0

# Press button
# Output:
# Event: time 1234.567890, type 1 (EV_KEY), code 28 (KEY_ENTER), value 1
# Event: time 1234.567890, type 0 (EV_SYN), code 0 (SYN_REPORT), value 0
#
# Release button
# Output:
# Event: time 1235.123456, type 1 (EV_KEY), code 28 (KEY_ENTER), value 0
# Event: time 1235.123456, type 0 (EV_SYN), code 0 (SYN_REPORT), value 0
```

### Test 3: Read Raw Events

```bash
# Read raw event bytes (blocking)
hexdump -C /dev/input/event0

# Press button → see binary data
# 00000000  12 34 56 78 90 ab cd ef  01 00 1c 00 01 00 00 00  |.4Vx............|
#           └─────── timestamp ─────┘  │  │  └─ value (1=press)
#                                      │  └─ code (28=KEY_ENTER)
#                                      └─ type (1=EV_KEY)
```

### Test 4: Verify All Interfaces Work

```bash
# Interface 1: sysfs (debug)
cat /sys/bus/platform/devices/bbb-flagship-button/press_count

# Interface 2: character device (custom app)
cat /dev/bbb-button

# Interface 3: input subsystem (standard app)
evtest /dev/input/event0

# All three should receive events simultaneously!
```

---

## Part 5: Advanced Features

### Feature 1: Multiple Key Codes

Support multiple buttons or key combinations:

```c
// In probe()
input_set_capability(b->input, EV_KEY, KEY_ENTER);
input_set_capability(b->input, EV_KEY, KEY_ESC);
input_set_capability(b->input, EV_KEY, KEY_SPACE);

// In work function
input_report_key(b->input, KEY_ENTER, pressed);
input_report_key(b->input, KEY_ESC, pressed);  // Simultaneous events
input_sync(b->input);
```

### Feature 2: Key Repeat

Enable automatic key repeat (like holding a keyboard key):

```c
// In probe() after input_allocate_device()
__set_bit(EV_REP, b->input->evbit);  // Enable repeat

// Default repeat: 250ms delay, 33ms period (30 Hz)
// Customize:
b->input->rep[REP_DELAY] = 500;   // 500ms before repeat
b->input->rep[REP_PERIOD] = 100;  // 100ms between repeats
```

### Feature 3: Long Press Detection

Detect short vs long press:

```c
struct bbb_btn {
    // ... existing fields ...
    ktime_t press_time;
    struct timer_list long_press_timer;
};

// In work function (on press)
if (!state) {  // Button pressed
    b->press_time = ktime_get();
    mod_timer(&b->long_press_timer, jiffies + msecs_to_jiffies(1000));
}

// In work function (on release)
if (state) {  // Button released
    s64 duration_ms = ktime_ms_delta(ktime_get(), b->press_time);
    
    del_timer(&b->long_press_timer);
    
    if (duration_ms < 1000) {
        // Short press
        input_report_key(b->input, KEY_ENTER, 1);
        input_report_key(b->input, KEY_ENTER, 0);
    } else {
        // Long press already handled by timer
    }
    input_sync(b->input);
}

// Timer callback
static void long_press_timer_cb(struct timer_list *t)
{
    struct bbb_btn *b = from_timer(b, t, long_press_timer);
    
    // Long press detected
    input_report_key(b->input, KEY_ESC, 1);  // Different key for long press
    input_report_key(b->input, KEY_ESC, 0);
    input_sync(b->input);
}
```

### Feature 4: LED Feedback

If your button has an LED, handle LED events:

```c
// In probe()
input_set_capability(b->input, EV_LED, LED_CAPSL);

// Add event handler
b->input->event = bbb_btn_input_event;

// Event handler
static int bbb_btn_input_event(struct input_dev *dev, unsigned int type,
                                unsigned int code, int value)
{
    struct bbb_btn *b = input_get_drvdata(dev);
    
    if (type == EV_LED && code == LED_CAPSL) {
        // Set LED based on value
        gpiod_set_value(b->led_gpio, value);
        return 0;
    }
    
    return -EINVAL;
}
```

### Feature 5: Absolute Position (Touchscreen-style)

If you add a rotary encoder or slider later:

```c
// In probe()
input_set_capability(b->input, EV_ABS, ABS_X);
input_set_abs_params(b->input, ABS_X, 0, 1023, 0, 0);  // Min, max, fuzz, flat

// In work function
input_report_abs(b->input, ABS_X, position);
input_sync(b->input);
```

---

## Part 6: Userspace Integration

### C Program Example

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main(void)
{
    int fd;
    struct input_event ev;
    
    fd = open("/dev/input/event0", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    printf("Monitoring button events (Ctrl+C to exit)...\n");
    
    while (1) {
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev)) {
            perror("read");
            break;
        }
        
        if (ev.type == EV_KEY && ev.code == KEY_ENTER) {
            printf("Button %s at %ld.%06ld\n",
                   ev.value ? "PRESSED" : "RELEASED",
                   ev.time.tv_sec, ev.time.tv_usec);
        }
    }
    
    close(fd);
    return 0;
}
```

**Compile and run:**
```bash
gcc -o button_monitor button_monitor.c
./button_monitor
```

### Python Example

```python
#!/usr/bin/env python3
import struct
import time

# Input event format: llHHI (long, long, unsigned short, unsigned short, unsigned int)
EVENT_FORMAT = 'llHHI'
EVENT_SIZE = struct.calcsize(EVENT_FORMAT)

EV_KEY = 1
KEY_ENTER = 28

with open('/dev/input/event0', 'rb') as f:
    print("Monitoring button events (Ctrl+C to exit)...")
    
    while True:
        event = f.read(EVENT_SIZE)
        (tv_sec, tv_usec, type, code, value) = struct.unpack(EVENT_FORMAT, event)
        
        if type == EV_KEY and code == KEY_ENTER:
            timestamp = tv_sec + tv_usec / 1000000.0
            state = "PRESSED" if value else "RELEASED"
            print(f"Button {state} at {timestamp:.6f}")
```

### Shell Script Example

```bash
#!/bin/bash
# Monitor button events

evtest /dev/input/event0 | while read line; do
    if echo "$line" | grep -q "KEY_ENTER"; then
        if echo "$line" | grep -q "value 1"; then
            echo "Button pressed - trigger action!"
            # Do something
        fi
    fi
done
```

---

## Part 7: Integration with GUI Systems

### X11 Integration

Your button automatically works as a keyboard key in X11!

```bash
# Test in X11 terminal
xev  # X Event Viewer

# Press your button → see KeyPress/KeyRelease events
# KeyPress event, serial 33, synthetic NO, window 0x3400001,
#     root 0x15a, subw 0x0, time 1234567, (100,100), root:(500,500),
#     state 0x0, keycode 36 (keysym 0xff0d, Return), same_screen YES,
```

### Wayland/libinput Integration

```bash
# Monitor with libinput
libinput debug-events

# Press button → see KEYBOARD_KEY event
# event2   KEYBOARD_KEY     +1.234s  KEY_ENTER (28) pressed
# event2   KEYBOARD_KEY     +1.567s  KEY_ENTER (28) released
```

### Remap Key Code

Use `udev` hwdb to remap key:

```bash
# /etc/udev/hwdb.d/90-button-remap.hwdb
evdev:name:BeagleBone Black Flagship Button:dmi:*
 KEYBOARD_KEY_1c=volumeup  # Remap KEY_ENTER to Volume Up

# Reload
systemd-hwdb update
udevadm trigger
```

---

## Part 8: Complete Code Example

### Modified bbb_flagship_button.c (Key Parts)

```c
#include <linux/input.h>

// In probe()
static int bbb_btn_probe(struct platform_device *pdev)
{
    struct bbb_btn *b;
    int ret;
    
    // ... existing setup ...
    
    /* Allocate input device */
    b->input = devm_input_allocate_device(&pdev->dev);
    if (!b->input)
        return -ENOMEM;
    
    /* Configure input device */
    b->input->name = "BeagleBone Black Flagship Button";
    b->input->phys = "bbb-flagship-button/input0";
    b->input->id.bustype = BUS_HOST;
    b->input->id.vendor = 0x0001;
    b->input->id.product = 0x0001;
    b->input->id.version = 0x0100;
    
    /* Declare capabilities */
    input_set_capability(b->input, EV_KEY, KEY_ENTER);
    
    /* Link driver data */
    input_set_drvdata(b->input, b);
    
    /* Register input device */
    ret = input_register_device(b->input);
    if (ret)
        return dev_err_probe(&pdev->dev, ret, "input registration failed\n");
    
    dev_info(&pdev->dev, "Input device registered as %s\n", b->input->name);
    
    return 0;
}

// In debounce work
static void bbb_btn_debounce_work(struct work_struct *work)
{
    struct bbb_btn *b = container_of(work, struct bbb_btn, debounce_work.work);
    int state;
    
    // ... existing debounce logic ...
    
    /* Report to input subsystem */
    input_report_key(b->input, KEY_ENTER, !state);  // !state for ACTIVE_LOW
    input_sync(b->input);
    
    // ... existing chardev notification ...
}
```

### No Changes Needed in remove()

The `devm_*` functions handle cleanup automatically!

---

## Part 9: Debugging Input Subsystem

### Check sysfs

```bash
# Find your input device
ls /sys/class/input/

# Output: input0  input1  event0  event1  ...

# Check capabilities
cat /sys/class/input/input0/name
# BeagleBone Black Flagship Button

cat /sys/class/input/input0/capabilities/key
# 10000000 0 0 0  (bit 28 set = KEY_ENTER)
```

### Trace Events with ftrace

```bash
# Enable input event tracing
echo 1 > /sys/kernel/debug/tracing/events/input/enable

# Press button

# Read trace
cat /sys/kernel/debug/tracing/trace

# Output:
#     kworker-123 [000] ....  1234.567890: input_event: dev=input0 type=1 code=28 value=1
#     kworker-123 [000] ....  1234.567891: input_event: dev=input0 type=0 code=0 value=0
```

### Check Device Registration

```bash
# List all handlers
cat /proc/bus/input/handlers

# Output:
# N: Number=0 Name=kbd
# N: Number=1 Name=mousedev
# N: Number=2 Name=evdev
# ...

# Your device uses evdev handler (event0)
```

---

## Part 10: Comparison of Three Interfaces

| Feature | sysfs | chardev | input |
|---------|-------|---------|-------|
| **Purpose** | Debug/stats | Custom apps | Standard apps |
| **API** | Text files | Custom protocol | `struct input_event` |
| **Tools** | `cat`, shell | Custom reader | `evtest`, `libinput` |
| **Blocking** | No (poll) | Yes | Yes |
| **Standard** | No | No | Yes |
| **GUI integration** | No | No | Yes |
| **Event format** | Custom text | Custom text | Binary struct |
| **Timestamp** | Manual | Manual | Automatic |
| **Multi-client** | Yes | No (single open) | Yes |

**Use case guide:**
- **sysfs**: Quick debugging, shell scripts, stats monitoring
- **chardev**: Custom apps with specific requirements, text-based logging
- **input**: GUI apps, standard tools, desktop integration, production use

---

## Part 11: Best Practices

### 1. Choose Appropriate Key Codes

```c
// Good: For general buttons
KEY_ENTER, KEY_ESC, KEY_SPACE

// Good: For function-specific buttons
KEY_POWER, KEY_VOLUMEUP, KEY_VOLUMEDOWN

// Good: For generic custom buttons
BTN_0, BTN_1, BTN_2, ... BTN_9

// Avoid: Random unused keys
KEY_F13, KEY_KPDOT  // Unless actually appropriate
```

### 2. Always Use input_sync()

```c
// Correct: Single event
input_report_key(input, KEY_ENTER, 1);
input_sync(input);  // ← REQUIRED

// Correct: Multiple simultaneous events
input_report_key(input, KEY_LEFTCTRL, 1);
input_report_key(input, KEY_C, 1);
input_sync(input);  // ← Groups both events
```

### 3. Handle Active-Low Correctly

```c
// GPIO is ACTIVE_LOW, so:
// gpiod_get_value() = 0 means PRESSED
// gpiod_get_value() = 1 means RELEASED

int state = gpiod_get_value(gpiod);
input_report_key(input, KEY_ENTER, !state);  // Invert for input subsystem
```

### 4. Use devm_* Functions

```c
// Good: Auto-cleanup
b->input = devm_input_allocate_device(&pdev->dev);

// Avoid: Manual cleanup
b->input = input_allocate_device();  // Need input_free_device() later
```

---

## Summary

### What You'll Build

✅ **Input device registration** - `/dev/input/eventX`  
✅ **Standard event protocol** - `struct input_event`  
✅ **GUI integration** - Works with X11, Wayland  
✅ **Standard tools** - `evtest`, `libinput-debug-events`  
✅ **Multi-interface** - sysfs + chardev + input all work  

### Code Changes

1. Add `#include <linux/input.h>`
2. Add `struct input_dev *input` to driver state
3. Allocate and configure in `probe()`
4. Call `input_report_key()` + `input_sync()` in IRQ handler
5. Cleanup automatic (via `devm_*`)

### Lines of Code

- **New code**: ~30 lines
- **Modified code**: ~10 lines
- **Total effort**: 30-60 minutes

### Testing

```bash
# Check device created
ls /dev/input/event*

# Monitor events
evtest /dev/input/event0

# Verify all interfaces
cat /sys/.../press_count  # sysfs
cat /dev/bbb-button       # chardev
evtest /dev/input/event0  # input
```

---

## Next Steps

1. **Implement basic input support** (30 min)
2. **Test with evtest** (10 min)
3. **Add device tree binding** for configurable key code (20 min)
4. **Write userspace test program** (30 min)
5. **Document in README** (15 min)
6. **Optional: Add advanced features** (long press, LED, etc.)

---

**Document Version:** 1.0  
**Author:** Chun  
**Project:** BeagleBone Black Flagship Driver Development  
**Date:** December 27, 2024

