#ifndef BBB_FLAGSHIP_BUTTON_H
#define BBB_FLAGSHIP_BUTTON_H

#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

/* Main driver state - shared by platform and chardev */
struct bbb_btn {
    // Platform device fields (existing)
    struct device *dev;
    struct gpio_desc *gpiod;
    int irq;
    atomic64_t press_count;
    atomic64_t last_event_ns;
    atomic64_t total_irqs;
    atomic64_t work_executions;
    u32 debounce_ms;
    ktime_t last_irq_time;
    struct delayed_work debounce_work;
    spinlock_t lock;
    int last_state;
    bool work_pending;
    
    struct {
        dev_t devt;
        struct cdev cdev;
        struct class *class;
        struct device *char_dev;
        
        // Event buffer
        char buffer[256];
        bool has_event;
        wait_queue_head_t wait;
        spinlock_t lock;
    } chardev;

    struct input_dev *input; 
};

/* Character device functions (implemented in _chardev.c) */
int bbb_chardev_register(struct bbb_btn *btn, struct device *parent);
void bbb_chardev_unregister(struct bbb_btn *btn);
void bbb_chardev_push_event(struct bbb_btn *btn, const char *msg);

#endif /* BBB_FLAGSHIP_BUTTON_H */