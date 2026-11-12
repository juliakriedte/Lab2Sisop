#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julia Kriedte");
MODULE_DESCRIPTION("Proc Driver");
MODULE_VERSION("0.0.1");

#define PROCFS_NAME "helloworld"

static char *greeting = NULL;
static int id = -1;

module_param(greeting, charp, 0644);
MODULE_PARM_DESC(greeting, "String fornecida pelo arquivo em /proc");

module_param(id, int, 0644);
MODULE_PARM_DESC(id, "Identificador fornecido pelo arquivo em /proc");

static ssize_t procfile_read(struct file *, char __user *, size_t, loff_t *);

static struct proc_ops proc_file_fops = {
    .proc_read = procfile_read
};

static struct proc_dir_entry *proc_file;

static int procdrv_init(void)
{
    pr_info("Inserting the Proc module\n");

    if (greeting == NULL || id < 0) {
        pr_alert("Missing parameters. Example: modprobe procdrv greeting=\"Ola, mundo!\" id=42\n");
        return -EINVAL;
    }

    proc_file = proc_create(PROCFS_NAME, 0644, NULL, &proc_file_fops);
    if (proc_file == NULL) {
        pr_alert("Could not initialize /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }
    pr_info("/proc/%s created\n", PROCFS_NAME);

    return 0;
}

static void procdrv_exit(void)
{
    proc_remove(proc_file);
    pr_info("Removed the Proc module\n");
}

static ssize_t procfile_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    static int reads = 0;

    pr_info("Calling procfile_read\n");

    if (*offset > 0)
        return 0;

    char tmpbuf[256];
    int tmplen = snprintf(tmpbuf, sizeof(tmpbuf), "[%d] %s, %d reads", id, greeting, ++reads);

    if (tmplen > len)
        tmplen = len;

    int error_count = copy_to_user(buffer, tmpbuf, tmplen);
    if (error_count != 0) {
        pr_alert("Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }

    *offset += tmplen;
    return tmplen;
}

module_init(procdrv_init);
module_exit(procdrv_exit);