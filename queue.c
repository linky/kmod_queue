#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>

#define PROC_NAME "queue_test"
#define MAX_QUEUE_SIZE (1024)
#define MAX_ELEM_SIZE (64*1024)

struct data {
    struct list_head next;
    char* source;
    uint16_t len;
    uint8_t on_disk;
};

static struct list_head queue;
static atomic_t queue_size = ATOMIC_INIT(0);

static DEFINE_MUTEX(queue_rlock);
static DEFINE_MUTEX(queue_wlock);

static int __load_data(struct data* d)
{
    char path[256];
    struct file* f;
    struct dentry *dir;

    snprintf(path, sizeof(path), "/root/mnt/%p", d->source);
    f = filp_open(path, O_RDONLY, 0700);
    dir = f->f_path.dentry;
    d->source = kmalloc(d->len, GFP_KERNEL);
    kernel_read(f, d->source, d->len, NULL); // vfs_read is deprecated
    filp_close(f, NULL);

    //vfs_unlink(dir->d_inode, dir, NULL);

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

    snprintf(path, sizeof(path), "/root/mnt/%p", d->source);
    f = filp_open(path, O_CREAT | O_RDWR | O_TRUNC, 0700);
    kernel_write(f, d->source, d->len, NULL); // vfs_write is deprecated

    filp_close(f, NULL);
    kfree(d->source);
    d->on_disk = 1;

    return 0;
}

ssize_t save_old_data(ssize_t nr)
{
    struct data* d;
    ssize_t ret = nr;
    struct list_head* head = &queue;

    if (nr <= 0)
        return 0;

    list_for_each_entry_reverse(d, head, next) {
        __save_data(d);

        if (--nr == 0)
            break;
    }

    return ret - nr;
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

    if (atomic_read(&queue_size) > 10)
        pr_info("saved %ld\n", save_old_data(5));

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

static long queue_ioctl(struct file* f, unsigned int ioctl, unsigned long count)
{
    return 0;
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

    return 0;
}

static void queue_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);
}


module_init(queue_init)
module_exit(queue_exit)
MODULE_LICENSE("GPL");