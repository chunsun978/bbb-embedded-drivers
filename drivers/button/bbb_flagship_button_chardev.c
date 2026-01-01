// SPDX-License-Identifier: GPL-2.0
/*
 * BBB Flagship Button Character Device Driver
 *
 * Character device driver for GPIO button with sysfs interface.
 * Binds to device tree node: compatible = "bbb,flagship-button"
 *
 * Author: Chun
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>    
#include <linux/fs.h>      
#include <linux/wait.h>    
#include "bbb_flagship_button_chardev.h"

#define DRV_NAME "bbb_flagship_button_chardev"





static int bbb_btn_chardev_open(struct inode *inode, struct file *file)
{
    struct bbb_btn *btn;

    btn = container_of(inode->i_cdev, struct bbb_btn, chardev.cdev);

    // store in file->private_data
    file->private_data = btn;

    dev_info(btn->chardev.char_dev, "bbb flagship button character device opened\n");

    return 0;
}

// static ssize_t bbb_btn_chardev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
// {
//     struct bbb_btn *btn = file->private_data;
//     int ret;
//     unsigned long flags;
//     size_t len;

//     // Block until event is available
//     ret = wait_event_interruptible(btn->chardev.wait, btn->chardev.has_event);
//     if (ret)
//         return -ERESTARTSYS;// signal received

//     // Copy event to user space
//     spin_lock_irqsave(&btn->chardev.lock, flags);
//     if (!btn->chardev.has_event) {
//         spin_unlock_irqrestore(&btn->chardev.lock, flags);
//         return -EAGAIN;
//     }

//     len = strlen(btn->chardev.buffer);
//     if (count < len)
//         len = count;

//     if (copy_to_user(buf, btn->chardev.buffer, len)) {
//         spin_unlock_irqrestore(&btn->chardev.lock, flags);
//         return -EFAULT;
//     }

//     btn->chardev.has_event = false;
//     spin_unlock_irqrestore(&btn->chardev.lock, flags);

//     return len;
// }


static ssize_t bbb_btn_chardev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct bbb_btn *btn = file->private_data;
    char local_buf[256];
    int ret;
    unsigned long flags;
    size_t len;

    /* Block until event is available */
    ret = wait_event_interruptible(btn->chardev.wait, btn->chardev.has_event);
    if (ret)
        return -ERESTARTSYS;

    // Copy event to LOCAL buffer first (while locked)
    spin_lock_irqsave(&btn->chardev.lock, flags);
    if (!btn->chardev.has_event) {
        spin_unlock_irqrestore(&btn->chardev.lock, flags);
        return -EAGAIN;
    }

    len = strlen(btn->chardev.buffer);
    if (len >= sizeof(local_buf))  // Safety check
        len = sizeof(local_buf) - 1;
    
    memcpy(local_buf, btn->chardev.buffer, len);  // Copy to local
    local_buf[len] = '\0';
    
    btn->chardev.has_event = false;
    spin_unlock_irqrestore(&btn->chardev.lock, flags);  // Release BEFORE copy_to_user!

    
    if (count < len)
        len = count;

    
    // Now copy to userspace (NOT in atomic context)
    if (copy_to_user(buf, local_buf, len)) {
        return -EFAULT;
    }

    return len;
}

static int bbb_btn_chardev_release(struct inode *inode, struct file *file)
{
    struct bbb_btn *btn = file->private_data;

    dev_info(btn->dev, "bbb flagship button character device closed\n");
    return 0;
}

static const struct file_operations bbb_btn_chardev_fops = {
    .owner = THIS_MODULE,
    .open = bbb_btn_chardev_open,
    .read = bbb_btn_chardev_read,
    .release = bbb_btn_chardev_release,
};

// static int __init bbb_btn_chardev_init(void)
// {
//     int ret;
//     struct bbb_btn_chardev *btn;

//     btn = devm_kzalloc(sizeof(*btn), GFP_KERNEL);
//     if (!btn)
//         return -ENOMEM;

//     cdev_init(&btn->cdev, &bbb_btn_chardev_fops);
//     btn->devt = MKDEV(0, 0);
//     ret = cdev_add(&btn->cdev, btn->devt, 1);
//     if (ret)
//         return ret;

//     btn->class = class_create(THIS_MODULE, DRV_NAME);
//     if (IS_ERR(btn->class)) {
//         cdev_del(&btn->cdev);
//         return PTR_ERR(btn->class);
//     }

//     btn->char_dev = device_create(btn->class, NULL, btn->devt, NULL, DRV_NAME);
//     if (IS_ERR(btn->char_dev)) {
//         class_destroy(btn->class);
//         cdev_del(&btn->cdev);
//         return PTR_ERR(btn->char_dev);
//     }

//     dev_info(btn->char_dev, "bbb flagship button character device registered\n");
//     return 0;
// }

// In bbb_flagship_button_chardev.c

int bbb_chardev_register(struct bbb_btn *btn, struct device *parent)
{
    int ret;
    
    // Allocate device number
    ret = alloc_chrdev_region(&btn->chardev.devt, 0, 1, "bbb-button");
    if (ret)
        return ret;
    
    // Initialize and add cdev
    cdev_init(&btn->chardev.cdev, &bbb_btn_chardev_fops);

    btn->chardev.cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&btn->chardev.cdev, btn->chardev.devt, 1);
    if (ret)
        goto err_unregister;
    
    // Create class and device
    btn->chardev.class = class_create(THIS_MODULE, "bbb-button");
    if (IS_ERR(btn->chardev.class)) {
        ret = PTR_ERR(btn->chardev.class);
        goto err_cdev_del;
    }
    
    btn->chardev.char_dev = device_create(btn->chardev.class, parent,
                                          btn->chardev.devt, NULL, "bbb-button");
    if (IS_ERR(btn->chardev.char_dev)) {
        ret = PTR_ERR(btn->chardev.char_dev);
        goto err_class_destroy;
    }
    
    // Initialize wait queue and spinlock
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

void bbb_chardev_unregister(struct bbb_btn *btn)
{
    wake_up_interruptible(&btn->chardev.wait);
    device_destroy(btn->chardev.class, btn->chardev.devt);
    class_destroy(btn->chardev.class);
    cdev_del(&btn->chardev.cdev);
    unregister_chrdev_region(btn->chardev.devt, 1);
}

void bbb_chardev_push_event(struct bbb_btn *btn, const char *msg)
{
    unsigned long flags;
    
    spin_lock_irqsave(&btn->chardev.lock, flags);
    strncpy(btn->chardev.buffer, msg, sizeof(btn->chardev.buffer) - 1);
    btn->chardev.buffer[sizeof(btn->chardev.buffer) - 1] = '\0';  // ADD THIS LINE!
    btn->chardev.has_event = true;
    spin_unlock_irqrestore(&btn->chardev.lock, flags);
    
    wake_up_interruptible(&btn->chardev.wait);
}

// void bbb_chardev_push_event(struct bbb_btn *btn, const char *msg)
// {
//     unsigned long flags;
    
//     spin_lock_irqsave(&btn->chardev.lock, flags);
//     strncpy(btn->chardev.buffer, msg, sizeof(btn->chardev.buffer) - 1);
//     btn->chardev.has_event = true;
//     spin_unlock_irqrestore(&btn->chardev.lock, flags);
    
//     wake_up_interruptible(&btn->chardev.wait);
// }


// static struct platform_driver bbb_btn_chardev_driver = {
//     .remove = bbb_btn_chardev_remove,
//     .driver = {
//         .name = DRV_NAME,
//         .of_match_table = bbb_btn_chardev_of_match,
//     },
// };

// module_platform_driver(bbb_btn_chardev_driver);

// MODULE_AUTHOR("Chun");
// MODULE_DESCRIPTION("BBB Flagship Button Character Device Driver");
// MODULE_LICENSE("GPL");