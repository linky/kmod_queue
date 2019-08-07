#include "queue.h"

static const char* storage_dir = "/root/mnt";

static struct list_head queue;
static atomic_t queue_size = ATOMIC_INIT(0);

static struct task_struct* thread;
static uint8_t thread_stopped;
DECLARE_WAIT_QUEUE_HEAD(wait_queue);
static atomic_t save_count = ATOMIC_INIT(0);

static DEFINE_MUTEX(queue_rlock);
static DEFINE_MUTEX(queue_wlock);

static int __load_data(struct data* d)
{
    char path[256];
    struct file* f;

    snprintf(path, sizeof(path), "%s/%p", storage_dir, d->source);
    f = filp_open(path, O_RDONLY, 0700);
    d->source = kmalloc(d->len, GFP_KERNEL);
    kernel_read(f, d->source, d->len, NULL); // vfs_read deprecated

    vfs_unlink(f->f_path.dentry->d_parent->d_inode, f->f_path.dentry, NULL);
    filp_close(f, NULL);

    d->on_disk = 0;

    return 0;
}

static int __save_data(struct data* d)
{
    char path[256];
    struct file* f;

    if (d->on_disk)
        return -EINVAL;

    pr_info("tail %s\n", d->source);

    snprintf(path, sizeof(path), "%s/%p", storage_dir, d->source);
    f = filp_open(path, O_CREAT | O_RDWR | O_TRUNC, 0700);
    kernel_write(f, d->source, d->len, NULL); // vfs_write deprecated

    filp_close(f, NULL);
    kfree(d->source);
    d->on_disk = 1;

    return 0;
}

static ssize_t push_back(struct file* file, const char __user* buf, size_t count, loff_t* ppos)
{
    ssize_t ret;
    struct data* d;

    if (count > MAX_ELEM_SIZE)
        return -EINVAL;

    if (atomic_read(&queue_size) >= MAX_QUEUE_SIZE)
        return -ENOMEM;

    if (mutex_lock_interruptible(&queue_wlock))
        return -ERESTARTSYS;

    d = kmalloc(sizeof(struct data), GFP_KERNEL);
    d->source = kmalloc(count, GFP_KERNEL);
    d->len = count;
    d->on_disk = 0;
    if (d->source == NULL) {
        kfree(d);
        ret = -ENOMEM;
        goto out;
    }

    ret = (copy_from_user(d->source, buf, count) ? -EFAULT : count);
    list_add_tail(&d->next, &queue);
    atomic_inc(&queue_size);
    pr_info("%s: count %zu ret %zd size %d\n", __FUNCTION__, count, ret, atomic_read(&queue_size));

out:
    mutex_unlock(&queue_wlock);

    return ret;
}

static ssize_t pop_front(struct file* file, char __user* buf, size_t count, loff_t* ppos)
{
    ssize_t ret;
    ssize_t to_write;
    struct data* d;

    mutex_lock(&queue_rlock);

    if (list_empty(&queue)) {
        mutex_unlock(&queue_rlock);
        return -ENODATA;
    }

    d = list_entry(queue.next, struct data, next);
    list_del(queue.next);
    atomic_dec(&queue_size);

    mutex_unlock(&queue_rlock);

    if (d->on_disk) {
        __load_data(d);
    }

    to_write = min((size_t) d->len, count);
    ret = (copy_to_user(buf, d->source, to_write) ? -EFAULT : to_write);
    kfree(d->source);
    kfree(d);
    pr_info("%s: count %zu ret %zd size %d\n", __FUNCTION__, count, ret, atomic_read(&queue_size));

    return ret;
}

static int save_old_data(ssize_t arg)
{
    struct data* d;
    ssize_t nr = arg;

    if (nr <= 0)
        return 0;

    list_for_each_entry_reverse(d, &queue, next) {
        mutex_lock(&queue_rlock);
        __save_data(d);
        mutex_unlock(&queue_rlock);

        if (atomic_dec_and_test(&save_count) == 0)
            break;
    }

    return 0;
}

static int save_old_data_async(void* arg)
{
    struct data* d;
    DECLARE_WAITQUEUE(wq, current);

    printk(KERN_INFO "kthread state is wait sleep\n");
    add_wait_queue(&wait_queue, &wq);

    // enter in a loop waiting on the queue untill termination event
    while (!thread_stopped)
    {
        printk(KERN_INFO "kthread state is TASK_INTERRUPTIBLE\n");
        set_current_state(TASK_INTERRUPTIBLE);
        // call the scheduler to release a CPU
        schedule();
        // the thread has been woken up, the thread is woken up in TASK_RUNNING state
        printk(KERN_INFO "thread has been woken up in\n");

        list_for_each_entry_reverse(d, &queue, next) {
            mutex_lock(&queue_rlock);
            __save_data(d);
            mutex_unlock(&queue_rlock);

            if (atomic_dec_and_test(&save_count))
                break;
        }
    }

    set_current_state(TASK_RUNNING);
    remove_wait_queue(&wait_queue, &wq);
    printk(KERN_INFO "kthread state is TASK_RUNNING\n");

    do_exit(0);

    return 0;
}

static long queue_ioctl(struct file* f, unsigned int ioctl, unsigned long count)
{
    pr_info("ioctl %u %lu\n", ioctl, count);

    if (ioctl == 1 && !atomic_read(&save_count)) {
        atomic_set(&save_count, count);
        save_old_data(count);
    } else if (ioctl == 0) {
        atomic_set(&save_count, count);
        wake_up(&wait_queue);
    }

    return 0;
}

static void queue_cleanup(void)
{
    struct data* d;
    char path[256];
    struct file* f;

    list_for_each_entry_reverse(d, &queue, next) {
        if (d->on_disk) {
            snprintf(path, sizeof(path), "%s/%p", storage_dir, d->source);
            f = filp_open(path, O_RDWR, 0700);
            vfs_unlink(f->f_path.dentry->d_parent->d_inode, f->f_path.dentry, NULL);
            filp_close(f, NULL);
        } else {
            kfree(d->source);
        }

        kfree(d);
    }
}

static int queue_init(void)
{
    static const struct file_operations queue_ops = {
            .owner	        = THIS_MODULE,
            .read	        = pop_front,
            .write	        = push_back,
            .unlocked_ioctl = queue_ioctl
    };

    INIT_LIST_HEAD(&queue);

    pr_info("create queue %s\n", PROC_NAME);
    proc_create(PROC_NAME, 0, NULL, &queue_ops);
    thread = kthread_create(save_old_data_async, NULL, "thread");
    wake_up_process(thread);

    return 0;
}

static void queue_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);

    if (thread) {
        thread_stopped = 1;
        kthread_stop(thread);
    }

    queue_cleanup();
    pr_info("unload queue %s\n", PROC_NAME);
}


module_init(queue_init)
module_exit(queue_exit)
MODULE_LICENSE("GPL");
