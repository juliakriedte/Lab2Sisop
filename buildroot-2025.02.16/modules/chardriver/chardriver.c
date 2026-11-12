#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julia Kriedte");
MODULE_DESCRIPTION("Character Device Driver");
MODULE_VERSION("0.0.3");

#define DEVICE_NAME "chardrv"

static int major;
static struct class *cls = NULL;
static struct device *dev = NULL;

static int number_opens = 0;

struct message {
    char *data;
    size_t size;
    struct list_head node;
};

static LIST_HEAD(messages);
static unsigned int pending = 0;

static unsigned int max_size = 256;
module_param(max_size, uint, 0444);
MODULE_PARM_DESC(max_size, "Tamanho maximo de cada mensagem, em bytes");

static unsigned int max_msgs = 4;
module_param(max_msgs, uint, 0444);
MODULE_PARM_DESC(max_msgs, "Numero maximo de mensagens pendentes na fila");

static ssize_t chardrv_read   (struct  file *,       char __user *, size_t, loff_t *);
static ssize_t chardrv_write  (struct  file *, const char __user *, size_t, loff_t *);
static int     chardrv_open   (struct inode *, struct file *);
static int     chardrv_release(struct inode *, struct file *);

static struct file_operations fops =
{
    .read    = chardrv_read,
    .write   = chardrv_write,
    .open    = chardrv_open,
    .release = chardrv_release
};

static int chardrv_init(void)
{
    pr_info("Inserting the Character Device\n");

    if (max_msgs == 0) {
        pr_err("max_msgs must be greater than 0\n");
        return -EINVAL;
    }

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        pr_err("Character Device failed to register a major number\n");
        return major;
    }
    pr_info("Character Device registered major %d\n", major);

    cls = class_create(DEVICE_NAME);
    if (IS_ERR(cls)) {
        pr_err("Character Device failed to register device class\n");
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(cls);
    }
    pr_info("Character Device registered class\n");

    dev = device_create(cls, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(dev)) {
        pr_err("Character Device failed to create device\n");
        class_destroy(cls);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(dev);
    }
    pr_info("Character Device registered device\n");

    return 0;
}

static void chardrv_exit(void)
{
    struct message *msg, *tmp;
    int count = 0;

    list_for_each_entry_safe(msg, tmp, &messages, node) {
        list_del(&msg->node);
        kfree(msg->data);
        kfree(msg);
        count++;
    }
    pending = 0;
    pr_info("Freed %d pending message(s)\n", count);

    device_destroy(cls, MKDEV(major, 0));
    class_destroy(cls);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("Removed the Character Device\n");
}

static int chardrv_open(struct inode *inodep, struct file *filep)
{
    pr_info("Device has been opened %d time(s)\n", ++number_opens);
    return 0;
}

static int chardrv_release(struct inode *inodep, struct file *filep)
{
    pr_info("Device successfully closed\n");
    return 0;
}

static ssize_t chardrv_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    struct message *msg;

    if (len > max_size) {
        pr_alert("Message too long (%zu > %u)\n", len, max_size);
        return -EINVAL;
    }

    msg = kmalloc(sizeof(struct message), GFP_KERNEL);
    if (msg == NULL) {
        pr_alert("Failed to allocate memory for the message node\n");
        return -ENOMEM;
    }

    msg->data = kmalloc(len + 1, GFP_KERNEL);
    if (msg->data == NULL) {
        pr_alert("Failed to allocate memory for the message data\n");
        kfree(msg);
        return -ENOMEM;
    }

    if (copy_from_user(msg->data, buffer, len) != 0) {
        pr_alert("Failed to receive the message from the user\n");
        kfree(msg->data);
        kfree(msg);
        return -EFAULT;
    }

    msg->data[len] = '\0';
    msg->size = len;

    if (pending >= max_msgs) {
        struct message *old = list_first_entry(&messages, struct message, node);

        list_del(&old->node);
        pr_info("Queue full: discarding oldest message (%zu characters)\n", old->size);
        kfree(old->data);
        kfree(old);
        pending--;
    }

    list_add_tail(&msg->node, &messages);
    pending++;
    pr_info("Queued a message with %zu characters (%u pending)\n", len, pending);

    return len;
}

static ssize_t chardrv_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    struct message *msg;
    size_t size;

    if (*offset > 0)
        return 0;

    if (list_empty(&messages)) {
        pr_info("No messages to read\n");
        return 0;
    }

    msg = list_first_entry(&messages, struct message, node);

    if (len < msg->size) {
        pr_alert("User buffer is too small for the message\n");
        return -EINVAL;
    }

    if (copy_to_user(buffer, msg->data, msg->size) != 0) {
        pr_alert("Failed to send the message to the user\n");
        return -EFAULT;
    }

    size = msg->size;

    list_del(&msg->node);
    pending--;
    kfree(msg->data);
    kfree(msg);

    pr_info("Sent and freed a message with %zu characters\n", size);

    *offset += size;
    return size;
}

module_init(chardrv_init);
module_exit(chardrv_exit);