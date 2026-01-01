// SPDX-License-Identifier: GPL-2.0
/*
 * BBB Flagship Button Driver
 *
 * Platform driver for GPIO button with IRQ handling and sysfs interface.
 * Binds to device tree node: compatible = "bbb,bbb-flagship-button"
 *
 * Author: Chun
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/ktime.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include "bbb_flagship_button_chardev.h"
#include <linux/input.h>

#define DRV_NAME "bbb_flagship_button"

/*
 * Sysfs show function for press_count
 *
 * This function is called when userspace reads:
 *   cat /sys/bus/platform/devices/bbb-flagship-button/press_count
 *
 * Hints:
 * - Use dev_get_drvdata(dev) to get struct bbb_btn *
 * - Use sysfs_emit(buf, "%lld\n", value) to format output
 * - Return the number of bytes written
 */
static ssize_t press_count_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct bbb_btn *b = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%lld\n", atomic64_read(&b->press_count));
}

/*
 * Sysfs show function for last_event_ns
 */
static ssize_t last_event_ns_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    struct bbb_btn *b = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%lld\n", atomic64_read(&b->last_event_ns));
}

/*
 * Sysfs show function for total_irqs (debug counter)
 */
static ssize_t total_irqs_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct bbb_btn *b = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%lld\n", atomic64_read(&b->total_irqs));
}

/*
 * Sysfs show function for work_executions (debug counter)
 */
static ssize_t work_executions_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct bbb_btn *b = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%lld\n", atomic64_read(&b->work_executions));
}

/* Define sysfs attributes */
static DEVICE_ATTR_RO(press_count);
static DEVICE_ATTR_RO(last_event_ns);
static DEVICE_ATTR_RO(total_irqs);
static DEVICE_ATTR_RO(work_executions);

static struct attribute *bbb_btn_attrs[] = {
    &dev_attr_press_count.attr,
    &dev_attr_last_event_ns.attr,
    &dev_attr_total_irqs.attr,
    &dev_attr_work_executions.attr,
    NULL,
};
ATTRIBUTE_GROUPS(bbb_btn);

/*
 * TODO: Implement IRQ handler
 *
 * This is called when the button triggers an interrupt.
 *
 * Hints:
 * - Get current time with ktime_get()
 * - Implement debounce: if time since last IRQ < debounce_ms, ignore
 * - Increment press counter
 * - Store timestamp
 * - Use dev_dbg() for debug logging
 * - Return IRQ_HANDLED
 */
static irqreturn_t bbb_btn_irq(int irq, void *data)
{
    struct bbb_btn *b = data;
    unsigned long flags;

    /* Debug: Count every IRQ (including bounces) */
    atomic64_inc(&b->total_irqs);

    /* Trace for detailed timing analysis */
    trace_printk("IRQ: count=%lld time=%lld\n", 
                 atomic64_read(&b->total_irqs), ktime_get_ns());

    spin_lock_irqsave(&b->lock, flags);

    /* Cancel any pending work and reschedule */
    /* This gives button time to settle */
    cancel_delayed_work(&b->debounce_work);
    schedule_delayed_work(&b->debounce_work,
                          msecs_to_jiffies(b->debounce_ms));
    b->work_pending = true;

    spin_unlock_irqrestore(&b->lock, flags);

    return IRQ_HANDLED;
}

/*
 * Debounce work function
 *
 * This is called after the debounce delay.
 *
 * Hints:
 * - Use gpiod_get_value_cansleep() to read GPIO state
 * - Only process if state actually changed
 */
 static void bbb_btn_debounce_work(struct work_struct *work)
 {
    struct bbb_btn *b = container_of(work, struct bbb_btn,
                                     debounce_work.work);
    int state;
    unsigned long flags;
    char msg[256];


    /* Read stable GPIO state after debounce delay */
    state = gpiod_get_value_cansleep(b->gpiod);

    /* Debug: Count work executions */
    atomic64_inc(&b->work_executions);

    /* Trace for detailed analysis */
    trace_printk("WORK: state=%d last=%d count=%lld time=%lld\n",
                 state, b->last_state, atomic64_read(&b->press_count),
                 ktime_get_ns());

    spin_lock_irqsave(&b->lock, flags);

    /* Only process if state actually changed */
    if (state != b->last_state) {
        b->last_state = state;
        atomic64_inc(&b->press_count);
        atomic64_set(&b->last_event_ns, ktime_get_ns());
        atomic64_inc(&b->work_executions);

        input_report_key(b->input, KEY_ENTER, !state);  // !state because GPIO_ACTIVE_LOW
        input_sync(b->input);


        dev_dbg(b->dev, "button %s: count=%lld\n",
                state ? "released" : "pressed",
                atomic64_read(&b->press_count));

    }

    b->work_pending = false;
    spin_unlock_irqrestore(&b->lock, flags);

    snprintf(msg, sizeof(msg), "button %s: count=%lld time=%lld\n",
                state ? "released" : "pressed",
                atomic64_read(&b->press_count),
                ktime_get_ns());            

    bbb_chardev_push_event(b, msg);

 }


/*
 * Probe function
 *
 * Called when kernel matches our compatible string with a DT node.
 *
 * Steps:
 * 1. Allocate driver data with devm_kzalloc()
 * 2. Read debounce-ms from DT with device_property_read_u32()
 * 3. Get GPIO with devm_gpiod_get(dev, "button", GPIOD_IN)
 * 4. Get IRQ with gpiod_to_irq()
 * 5. Request threaded IRQ with devm_request_threaded_irq()
 * 6. Initialize counters
 * 7. Log success with dev_info()
 *
 * Hints:
 * - Use dev_set_drvdata() to attach struct to the device
 * - Use dev_err_probe() for error handling
 * - All devm_* functions auto-cleanup on remove/error
 */
static int bbb_btn_probe(struct platform_device *pdev)
{
    struct bbb_btn *b;
    int ret = 0;

    b = devm_kzalloc(&pdev->dev, sizeof(*b), GFP_KERNEL);
    if (!b)
        return -ENOMEM;

    b->dev = &pdev->dev;
    dev_set_drvdata(&pdev->dev, b);

    /* Read optional debounce-ms */
    b->debounce_ms = 20;
    device_property_read_u32(&pdev->dev, "debounce-ms", &b->debounce_ms);

    /* Get GPIO from DT: "button-gpios" */
    b->gpiod = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);
    if (IS_ERR(b->gpiod))
        return dev_err_probe(&pdev->dev, PTR_ERR(b->gpiod),
                             "failed to get button gpio\n");

    b->irq = gpiod_to_irq(b->gpiod);
    if (b->irq < 0)
        return dev_err_probe(&pdev->dev, b->irq, "gpiod_to_irq failed\n");

    /* Initialize counters */
    atomic64_set(&b->press_count, 0);
    atomic64_set(&b->last_event_ns, 0);
    atomic64_set(&b->total_irqs, 0);
    atomic64_set(&b->work_executions, 0);
    b->last_irq_time = ktime_set(0, 0);

    /* Request IRQ on both edges to capture press/release if desired */
    ret = devm_request_threaded_irq(&pdev->dev, b->irq,
                                    NULL,              /* top-half */
                                    bbb_btn_irq,       /* threaded handler */
                                    IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                                    DRV_NAME, b);
    if (ret)
        return dev_err_probe(&pdev->dev, ret, "request_irq failed\n");

    /* sysfs files are automatically created by dev_groups in driver struct */
    spin_lock_init(&b->lock);
    INIT_DELAYED_WORK(&b->debounce_work, bbb_btn_debounce_work);
    b->last_state = gpiod_get_value(b->gpiod);
    b->work_pending = false;

    ret = bbb_chardev_register(b, &pdev->dev);
    if (ret)
        return dev_err_probe(&pdev->dev, ret, "chardev registration failed\n");

    /* Allocate input device */
    b->input = devm_input_allocate_device(&pdev->dev);
    if (!b->input) {
        bbb_chardev_unregister(b);
        return -ENOMEM;
    }

    /* Configure input device */
    b->input->name = "BeagleBone Black Flagship Button";
    b->input->phys = "bbb-flagship-button/input0";
    b->input->id.bustype = BUS_HOST;
    b->input->id.vendor = 0x0001;
    b->input->id.product = 0x0001;
    b->input->id.version = 0x0100;

    /* Set input capability: we report KEY_ENTER events */
    input_set_capability(b->input, EV_KEY, KEY_ENTER);

    /* Associate driver data with input device */
    input_set_drvdata(b->input, b);

    /* Register the input device */
    ret = input_register_device(b->input);
    if (ret) {
        bbb_chardev_unregister(b);
        return dev_err_probe(&pdev->dev, ret, "input registration failed\n");
    }

    dev_info(&pdev->dev, "driver loaded (irq=%d, debounce=%u ms, input=%s)\n",
            b->irq, b->debounce_ms, b->input->name);         

    return 0;
}

/*
 * Remove function - cleanup when driver unloads
 *
 * Note: If use devm_* functions in probe, most cleanup is automatic!
 */
static void bbb_btn_remove(struct platform_device *pdev)
{
    struct bbb_btn *b = platform_get_drvdata(pdev);
    cancel_delayed_work_sync(&b->debounce_work);
    bbb_chardev_unregister(b);
    dev_info(&pdev->dev, "bbb flagship button driver removed\n");
}

/* Device Tree match table */
static const struct of_device_id bbb_btn_of_match[] = {
    { .compatible = "bbb,flagship-button" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bbb_btn_of_match);

/* Platform driver structure */
static struct platform_driver bbb_btn_driver = {
    .probe      = bbb_btn_probe,
    .remove_new = bbb_btn_remove,
    .driver = {
        .name = DRV_NAME,
        .of_match_table = bbb_btn_of_match,
        .dev_groups = bbb_btn_groups,
    },
};

/* Register as platform driver */
module_platform_driver(bbb_btn_driver);

MODULE_AUTHOR("Chun");
MODULE_DESCRIPTION("BeagleBone Black Flagship GPIO/IRQ Button Driver");
MODULE_LICENSE("GPL");

