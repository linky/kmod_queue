#ifndef __QUEUE_H
#define __QUEUE_H

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <asm/atomic.h>

#define PROC_NAME "queue_test"
#define MAX_QUEUE_SIZE (1024)
#define MAX_ELEM_SIZE (64*1024)

enum { SAVE_SYNC = 1000, SAVE_ASYNC };

struct data {
    struct list_head next;
    char* source;
    uint16_t len;
    uint8_t on_disk;
};

static int __load_data(struct data* d);
static int __save_data(struct data* d);
static int save_old_data(ssize_t arg);
static int save_old_data_async(void* arg);
static ssize_t push_back(struct file* file, const char __user* buf, size_t count, loff_t* ppos);
static ssize_t pop_front(struct file* file, char __user* buf, size_t count, loff_t* ppos);
static long queue_ioctl(struct file* f, unsigned int ioctl, unsigned long count);
static void queue_cleanup(void);

#endif /* __QUEUE_H */