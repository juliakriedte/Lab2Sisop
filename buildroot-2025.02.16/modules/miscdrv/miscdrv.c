#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julia Kriedte");
MODULE_DESCRIPTION("Misc Device Driver");
MODULE_VERSION("0.0.1");

#define DEVICE_NAME "miscdrv"

static int number_opens = 0;
static char message[256] = {};
static size_t size_of_message = 0;

static ssize_t  miscdrv_read   (struct  file *,       char __user *, size_t, loff_t *);
static ssize_t  miscdrv_write  (struct  file *, const char __user *, size_t, loff_t *);
static int      miscdrv_open   (struct inode *, struct file *);
static int      miscdrv_release(struct inode *, struct file *);

static struct file_operations fops =
{
    .read    = miscdrv_read,
    .write   = miscdrv_write,
    .open    = miscdrv_open,
    .release = miscdrv_release
};

static struct miscdevice miscdrv_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &fops,
    .mode  = 0644
};

static int miscdrv_init(void)
{
    int err;

    pr_info("Inserting the Misc Device\n");

    err = misc_register(&miscdrv_dev);
    if (err != 0) {
        pr_err("Misc Device failed to register\n");
        return err;
    }
    pr_info("Misc Device registered with minor %d\n", miscdrv_dev.minor);

    return 0;
}

static void miscdrv_exit(void)
{
    misc_deregister(&miscdrv_dev);
    pr_info("Removed the Misc Device\n");
}

static int miscdrv_open(struct inode *inodep, struct file *filep)
{
    number_opens++;
    pr_info("Device has been opened %d time(s)\n", number_opens);

    return 0;
}

static int miscdrv_release(struct inode *inodep, struct file *filep)
{
    pr_info("Device successfully closed\n");
    return 0;
}

static ssize_t miscdrv_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    if (len >= sizeof(message)) {
        pr_alert("Too many characters to deal with (%zu)\n", len);
        return -EINVAL;
    }

    if (copy_from_user(message, buffer, len) != 0) {
        pr_alert("Failed to receive characters from the user\n");
        return -EFAULT;
    }

    message[len] = '\0';
    size_of_message = len;

    pr_info("Received %zu characters from the user\n", len);
    return len;
}

static ssize_t miscdrv_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    pr_info("Sending up to %zu characters to the user\n", len);
    return simple_read_from_buffer(buffer, len, offset, message, size_of_message);
}

module_init(miscdrv_init);
module_exit(miscdrv_exit);