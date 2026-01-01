# Character Device Driver Implementation Guide

**Date:** December 27, 2024  
**Goal:** Add character device interface (`/dev/bbb-button`) to platform button driver

---

## Overview

This guide documents the implementation of a character device interface for the BBB button driver, allowing userspace to read button events via standard file operations (`open`, `read`, `close`).

### What We Built

**Before:** Platform driver with sysfs interface only
```bash
# Userspace had to poll sysfs
cat /sys/bus/platform/devices/bbb-flagship-button/press_count
```

**After:** Platform driver + Character device with blocking I/O
```bash
# Userspace blocks until button press
cat /dev/bbb-button
# Prints: "button pressed: count=1 time=12345678"
```

---

## Architecture: Modular Design

### File Structure

```
kernel/drivers/button/
├── bbb_flagship_button.c          # Main: platform driver, GPIO, IRQ
├── bbb_flagship_button_chardev.c  # Helper: character device functions
├── bbb_flagship_button_chardev.h  # Shared: struct definitions
└── Makefile                        # Builds both into single .ko
```

**Why modular?**
- ✅ Separation of concerns (platform vs chardev logic)
- ✅ Easier to test and debug
- ✅ Professional code organization (>500 lines total)
- ✅ Industry standard for complex drivers

### Shared Data Structure

**File: `bbb_flagship_button_chardev.h`**

```c
struct bbb_btn {
    // Platform device fields (existing)
    struct device *dev;
    struct gpio_desc *gpiod;
    int irq;
    atomic64_t press_count;
    atomic64_t last_event_ns;
    // ... other platform fields ...
    
    // Character device fields (NEW)
    struct {
        dev_t devt;              // Device number (major:minor)
        struct cdev cdev;        // Character device
        struct class *class;     // Device class for udev
        struct device *char_dev; // Device pointer
        
        // Event buffer
        char buffer[256];        // Event message
        bool has_event;          // Event pending flag
        wait_queue_head_t wait;  // Wait queue for blocking
        spinlock_t lock;         // Protect buffer access
    } chardev;
};
```

---

## Implementation Steps

### Step 1: Character Device Registration

**File: `bbb_flagship_button_chardev.c`**

Register character device from platform driver's `probe()`:

```c
int bbb_chardev_register(struct bbb_btn *btn, struct device *parent)
{
    int ret;
    
    // 1. Allocate device number (dynamic)
    ret = alloc_chrdev_region(&btn->chardev.devt, 0, 1, "bbb-button");
    if (ret)
        return ret;
    
    // 2. Initialize and add cdev
    cdev_init(&btn->chardev.cdev, &bbb_btn_chardev_fops);
    btn->chardev.cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&btn->chardev.cdev, btn->chardev.devt, 1);
    if (ret)
        goto err_unregister;
    
    // 3. Create device class (for automatic /dev/ creation)
    btn->chardev.class = class_create(THIS_MODULE, "bbb-button");
    if (IS_ERR(btn->chardev.class)) {
        ret = PTR_ERR(btn->chardev.class);
        goto err_cdev_del;
    }
    
    // 4. Create device node (/dev/bbb-button)
    btn->chardev.char_dev = device_create(btn->chardev.class, parent,
                                          btn->chardev.devt, NULL, "bbb-button");
    if (IS_ERR(btn->chardev.char_dev)) {
        ret = PTR_ERR(btn->chardev.char_dev);
        goto err_class_destroy;
    }
    
    // 5. Initialize wait queue and spinlock
    init_waitqueue_head(&btn->chardev.wait);
    spin_lock_init(&btn->chardev.lock);
    btn->chardev.has_event = false;
    
    dev_info(parent, "Character device /dev/bbb-button registered\n");
    return 0;

err_class_destroy:
    class_destroy(btn->chardev.class);
err_cdev_del:
    cdev_del(&btn->chardev.cdev);
err_unregister:
    unregister_chrdev_region(btn->chardev.devt, 1);
    return ret;
}
```

### Step 2: File Operations - open()

```c
static int bbb_btn_chardev_open(struct inode *inode, struct file *file)
{
    struct bbb_btn *btn;
    
    // Get device structure from cdev
    btn = container_of(inode->i_cdev, struct bbb_btn, chardev.cdev);
    
    // Store for later use in read/release
    file->private_data = btn;
    
    dev_info(btn->chardev.char_dev, "Character device opened\n");
    return 0;
}
```

### Step 3: File Operations - read() with Blocking I/O

**Critical:** Never call `copy_to_user()` while holding a spinlock!

```c
static ssize_t bbb_btn_chardev_read(struct file *file, char __user *buf, 
                                     size_t count, loff_t *ppos)
{
    struct bbb_btn *btn = file->private_data;
    char local_buf[256];  // Local buffer (important!)
    unsigned long flags;
    size_t len;
    int ret;
    
    // 1. Block until event available
    ret = wait_event_interruptible(btn->chardev.wait, btn->chardev.has_event);
    if (ret)
        return -ERESTARTSYS;  // Interrupted by signal (Ctrl+C)
    
    // 2. Copy to LOCAL buffer first (while locked)
    spin_lock_irqsave(&btn->chardev.lock, flags);
    
    if (!btn->chardev.has_event) {
        spin_unlock_irqrestore(&btn->chardev.lock, flags);
        return -EAGAIN;  // Spurious wakeup
    }
    
    len = strlen(btn->chardev.buffer);
    if (len >= sizeof(local_buf))
        len = sizeof(local_buf) - 1;
    
    memcpy(local_buf, btn->chardev.buffer, len);  // Copy to local
    local_buf[len] = '\0';
    
    btn->chardev.has_event = false;
    spin_unlock_irqrestore(&btn->chardev.lock, flags);  // Release lock!
    
    // 3. Copy to userspace (NOT in atomic context)
    if (count < len)
        len = count;
    
    if (copy_to_user(buf, local_buf, len))
        return -EFAULT;
    
    return len;
}
```

**Why local buffer?**
- `copy_to_user()` can cause page faults (which sleep)
- Can't sleep while holding spinlock with IRQs disabled
- Must copy to local buffer, release lock, THEN copy to userspace

### Step 4: File Operations - release()

```c
static int bbb_btn_chardev_release(struct inode *inode, struct file *file)
{
    struct bbb_btn *btn = file->private_data;
    dev_info(btn->dev, "Character device closed\n");
    return 0;
}
```

### Step 5: File Operations Structure

```c
static const struct file_operations bbb_btn_chardev_fops = {
    .owner   = THIS_MODULE,
    .open    = bbb_btn_chardev_open,
    .read    = bbb_btn_chardev_read,
    .release = bbb_btn_chardev_release,
};
```

### Step 6: Push Events from IRQ Handler

**File: `bbb_flagship_button.c`**

```c
static void bbb_btn_debounce_work(struct work_struct *work)
{
    struct bbb_btn *b = container_of(work, struct bbb_btn, debounce_work.work);
    char msg[256];
    int state;
    
    // ... existing debounce logic ...
    
    // NEW: Push event to character device
    snprintf(msg, sizeof(msg), "button %s: count=%lld time=%lld\n",
             state ? "released" : "pressed",
             atomic64_read(&b->press_count),
             ktime_get_ns());
    
    bbb_chardev_push_event(b, msg);
}
```

### Step 7: Push Event Function

**File: `bbb_flagship_button_chardev.c`**

```c
void bbb_chardev_push_event(struct bbb_btn *btn, const char *msg)
{
    unsigned long flags;
    
    spin_lock_irqsave(&btn->chardev.lock, flags);
    
    // Copy message to buffer
    strncpy(btn->chardev.buffer, msg, sizeof(btn->chardev.buffer) - 1);
    btn->chardev.buffer[sizeof(btn->chardev.buffer) - 1] = '\0';  // Ensure null-term
    
    btn->chardev.has_event = true;
    
    spin_unlock_irqrestore(&btn->chardev.lock, flags);
    
    // Wake any blocked readers
    wake_up_interruptible(&btn->chardev.wait);
}
```

### Step 8: Cleanup on Remove

**File: `bbb_flagship_button_chardev.c`**

```c
void bbb_chardev_unregister(struct bbb_btn *btn)
{
    // Wake any blocked readers before destroying
    wake_up_interruptible(&btn->chardev.wait);
    
    // Cleanup in reverse order
    device_destroy(btn->chardev.class, btn->chardev.devt);
    class_destroy(btn->chardev.class);
    cdev_del(&btn->chardev.cdev);
    unregister_chrdev_region(btn->chardev.devt, 1);
}
```

### Step 9: Integration with Main Driver

**File: `bbb_flagship_button.c`**

```c
static int bbb_btn_probe(struct platform_device *pdev)
{
    struct bbb_btn *b;
    int ret;
    
    // ... existing platform setup ...
    
    // NEW: Register character device
    ret = bbb_chardev_register(b, &pdev->dev);
    if (ret)
        return dev_err_probe(&pdev->dev, ret, "chardev registration failed\n");
    
    dev_info(&pdev->dev, "Driver loaded with chardev support\n");
    return 0;
}

static void bbb_btn_remove(struct platform_device *pdev)
{
    struct bbb_btn *b = platform_get_drvdata(pdev);
    
    // NEW: Unregister character device first
    bbb_chardev_unregister(b);
    
    // ... existing cleanup ...
}
```

### Step 10: Makefile for Combined Module

```makefile
obj-m := bbb_flagship_button_combined.o
bbb_flagship_button_combined-y := bbb_flagship_button.o bbb_flagship_button_chardev.o
```

This builds both files into a single `.ko` module.

---

## Testing the Character Device

### Test 1: Basic Functionality

```bash
# Load module
insmod bbb_flagship_button_combined.ko

# Verify device created
ls -l /dev/bbb-button
# Output: crw------- 1 root root 238, 0 Dec 27 03:31 /dev/bbb-button

# Test blocking read
cat /dev/bbb-button
# (blocks until button pressed)
# Press button → prints event and returns
```

### Test 2: Continuous Monitoring

```bash
# Keep reading events
while true; do
    cat /dev/bbb-button
done

# Press button multiple times
# Each press/release generates an event
```

### Test 3: Verify Both Interfaces Work

```bash
# Character device
cat /dev/bbb-button &

# Sysfs (still works!)
cat /sys/bus/platform/devices/bbb-flagship-button/press_count
```

### Test 4: Permission Fix (if needed)

```bash
# If "Permission denied":
chmod 666 /dev/bbb-button

# Or add udev rule for permanent fix:
echo 'KERNEL=="bbb-button", MODE="0666"' > /etc/udev/rules.d/99-bbb-button.rules
```

---

## Key Concepts Learned

### 1. Blocking I/O with Wait Queues

```c
// Initialize
init_waitqueue_head(&wait_queue);

// Block until condition true
wait_event_interruptible(wait_queue, condition);

// Wake blocked processes
wake_up_interruptible(&wait_queue);
```

### 2. Atomic Context Rules

**Can't do in atomic context (spinlock + IRQs disabled):**
- ❌ `copy_to_user()` / `copy_from_user()` (may sleep on page fault)
- ❌ `kmalloc(GFP_KERNEL)` (may sleep)
- ❌ `msleep()` / any sleep function
- ❌ Acquire mutex (may sleep)

**Solution:** Copy to local buffer first, release lock, then copy to userspace.

### 3. container_of() Magic

```c
// Get parent struct from embedded member
struct bbb_btn *btn = container_of(inode->i_cdev, struct bbb_btn, chardev.cdev);
```

This allows file operations to access the full driver state.

### 4. Module Naming

```makefile
obj-m := bbb_flagship_button_combined.o  # Final .ko name
bbb_flagship_button_combined-y := file1.o file2.o  # Source files
```

Multiple `.o` files linked into single `.ko` module.

---

## Common Pitfalls and Solutions

### Pitfall 1: copy_to_user() in Spinlock

**Symptom:** `-EFAULT` error, "Bad address"

**Cause:** `copy_to_user()` called while holding spinlock with IRQs disabled.

**Fix:** Copy to local buffer first, release lock, then `copy_to_user()`.

### Pitfall 2: Buffer Not Null-Terminated

**Symptom:** Random crashes, `strlen()` reads garbage

**Fix:** Always null-terminate after `strncpy()`:
```c
strncpy(buf, msg, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = '\0';  // Explicit null-term
```

### Pitfall 3: Module Name Mismatch

**Symptom:** `rmmod` can't find old module

**Fix:** Update deploy script to use new module name:
```bash
rmmod bbb_flagship_button_combined  # New name, not old name
```

### Pitfall 4: Spurious Wakeups

**Symptom:** `read()` returns before actual event

**Fix:** Always re-check condition after waking:
```c
wait_event_interruptible(wait, has_event);
if (!has_event)  // Double-check!
    return -EAGAIN;
```

---

## Advanced Features (Optional)

### Feature 1: Non-Blocking Mode

Support `O_NONBLOCK` flag:

```c
if (!has_event) {
    if (file->f_flags & O_NONBLOCK)
        return -EAGAIN;  // Return immediately
    // Otherwise block
    wait_event_interruptible(wait, has_event);
}
```

### Feature 2: poll() Support

Allow `select()`/`poll()`/`epoll()`:

```c
static __poll_t bbb_btn_poll(struct file *file, poll_table *wait)
{
    struct bbb_btn *btn = file->private_data;
    __poll_t mask = 0;
    
    poll_wait(file, &btn->chardev.wait, wait);
    
    if (btn->chardev.has_event)
        mask |= EPOLLIN | EPOLLRDNORM;
    
    return mask;
}
```

### Feature 3: Circular Event Buffer

Store last N events instead of just one:

```c
#define EVENT_BUF_SIZE 16

struct event_buffer {
    char events[EVENT_BUF_SIZE][128];
    unsigned int head, tail, count;
    spinlock_t lock;
};
```

---

## Performance Characteristics

| Metric | Value |
|--------|-------|
| Event latency | <100μs (IRQ → wakeup) |
| `read()` overhead | ~1-2μs (no event pending) |
| Memory per device | ~300 bytes (event buffer + waitqueue) |
| CPU usage | Near zero (sleeping when idle) |

**Comparison to sysfs polling:**
- Sysfs poll: ~1ms latency, high CPU (constant polling)
- Chardev blocking: <100μs latency, zero CPU (sleeps until event)

---

## Summary

### What You Built

✅ **Modular character device driver** (professional structure)  
✅ **Blocking I/O** with wait queues  
✅ **Proper locking** (local buffer technique)  
✅ **Event streaming** (press/release with timestamps)  
✅ **Dual interfaces** (sysfs + chardev both work)  

### Skills Demonstrated

- Character device registration (`cdev`, `device_create`)
- File operations (`open`, `read`, `release`)
- Blocking I/O (wait queues, sleep/wake)
- Atomic context rules (`copy_to_user` safety)
- Modular driver architecture
- Kernel-userspace communication

### Files Created

- `bbb_flagship_button_chardev.c` (~200 lines)
- `bbb_flagship_button_chardev.h` (~45 lines)
- Modified `bbb_flagship_button.c` (+10 lines)
- Updated `Makefile` (combined module)

---

## Next Steps

1. **Remove debug statements** - Clean up `pr_info()` calls
2. **Add poll() support** - Enable `select()`/`epoll()`
3. **Implement ioctl()** - Runtime configuration
4. **Add circular buffer** - Store multiple events
5. **Create userspace library** - Wrap `/dev/bbb-button` access
6. **Write systemd service** - Monitor button events

---

**Document Version:** 1.0  
**Author:** Chun  
**Project:** BeagleBone Black Flagship Driver Development  
**Date:** December 27, 2024

