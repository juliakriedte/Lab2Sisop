#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/hrtimer.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julia Kriedte");
MODULE_DESCRIPTION("Timer Module");
MODULE_VERSION("0.0.2");

#define TIMEOUT_NSEC 0
#define PROCFS_NAME  "timerdrv_count"

static unsigned int timeout_sec = 1;
static atomic_t msg_count = ATOMIC_INIT(0);

static struct hrtimer hr_timer;
static struct kobject *timer_kobj;
static struct proc_dir_entry *proc_file;

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
    int n = atomic_inc_return(&msg_count);

    pr_info("Hello from timer! (%d)\n", n);

    hrtimer_forward_now(timer,
                        ktime_set(READ_ONCE(timeout_sec), TIMEOUT_NSEC));
    return HRTIMER_RESTART;
}

static ssize_t timeout_sec_show(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u\n", READ_ONCE(timeout_sec));
}

static ssize_t timeout_sec_store(struct kobject *kobj,
                                 struct kobj_attribute *attr,
                                 const char *buf, size_t count)
{
    unsigned int val;
    int err = kstrtouint(buf, 10, &val);

    if (err)
        return err;
    if (val == 0)
        return -EINVAL;

    WRITE_ONCE(timeout_sec, val);

    hrtimer_start(&hr_timer, ktime_set(val, TIMEOUT_NSEC), HRTIMER_MODE_REL);

    pr_info("timeout_sec changed to %u, timer restarted\n", val);
    return count;
}

static struct kobj_attribute timeout_sec_attr =
    __ATTR(timeout_sec, 0644, timeout_sec_show, timeout_sec_store);

static ssize_t procfile_read(struct file *filep, char __user *buffer,
                             size_t len, loff_t *offset)
{
    char tmpbuf[32];
    int tmplen = scnprintf(tmpbuf, sizeof(tmpbuf), "%d\n",
                           atomic_read(&msg_count));

    return simple_read_from_buffer(buffer, len, offset, tmpbuf, tmplen);
}

static const struct proc_ops proc_file_fops = {
    .proc_read = procfile_read
};

static int timerdrv_init(void)
{
    int err;

    pr_info("Inserting the Timer module\n");

    proc_file = proc_create(PROCFS_NAME, 0444, NULL, &proc_file_fops);
    if (!proc_file)
        return -ENOMEM;

    timer_kobj = kobject_create_and_add("timerdrv", kernel_kobj);
    if (!timer_kobj) {
        err = -ENOMEM;
        goto err_proc;
    }

    err = sysfs_create_file(timer_kobj, &timeout_sec_attr.attr);
    if (err)
        goto err_kobj;

    hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    hr_timer.function = timer_callback;
    hrtimer_start(&hr_timer, ktime_set(timeout_sec, TIMEOUT_NSEC),
                  HRTIMER_MODE_REL);

    return 0;

err_kobj:
    kobject_put(timer_kobj);
err_proc:
    proc_remove(proc_file);
    return err;
}

static void timerdrv_exit(void)
{
    kobject_put(timer_kobj);
    hrtimer_cancel(&hr_timer);
    proc_remove(proc_file);
    pr_info("Removed the Timer module\n");
}

module_init(timerdrv_init);
module_exit(timerdrv_exit);