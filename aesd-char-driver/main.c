/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Tej"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// Function Prototypes
static int aesd_open(struct inode *inode, struct file *filp);
static int aesd_release(struct inode *inode, struct file *filp);
static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos);
static ssize_t aesd_write(struct file *filp,
                    const char __user *buf, size_t count,
                    loff_t *f_pos);
static int aesd_setup_cdev(struct aesd_dev *dev);

static int aesd_init_module(void);
static void aesd_cleanup_module(void);

// Function Definitions
static int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */

    struct aesd_dev *dev = container_of(inode->i_cdev,
                                        struct aesd_dev, 
                                        cdev);
    filp->private_data = dev;
    
    return 0;
}

static int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */

    // Do nothing
    return 0;
}

static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    
    struct aesd_dev *dev = filp->private_data;
    mutex_lock(&(dev->lock));

    if(count <= 0){
        goto EXIT_READ;
    }
    
    ssize_t upto_entry_offset;
    struct aesd_buffer_entry *upto_entry = 
                                aesd_circular_buffer_find_entry_offset_for_fpos(
                                &(dev->circular_buffer),
                                (*f_pos) + count,
                                &upto_entry_offset
                            );


    if(upto_entry!=NULL){
        size_t num_bytes_remaining = count;
        loff_t offset = *f_pos;
        while(1){
            ssize_t curr_entry_offset;
            struct aesd_buffer_entry *curr_entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                                                        &(dev->circular_buffer),
                                                        offset,
                                                        &curr_entry_offset);

            size_t num_bytes_unread_in_curr = curr_entry->size - curr_entry_offset;
            if(num_bytes_unread_in_curr >= num_bytes_remaining){
                // all data within current element
                // copy count bytes         
                unsigned long bytes_not_copied = copy_to_user(
                                                        buf,
                                                        &(curr_entry[curr_entry_offset]),
                                                        num_bytes_remaining);
                            // MUST: this can copy less bytes than wanted to read
                            //          Retry until all read
                                    
                                                        
                *f_pos += retval;   
                retval = count - bytes_not_copied;
                goto EXIT_READ;
            }
            else{
                // some data in next element
                // copy until end of this entry
                unsigned long bytes_not_copied = copy_to_user(buf,
                                             &(curr_entry[curr_entry_offset]),
                                             num_bytes_unread_in_curr);

                size_t num_bytes_read = num_bytes_unread_in_curr - bytes_not_copied;

                num_bytes_remaining -= num_bytes_read;
                offset += num_bytes_read;
            }
        }
    }

EXIT_READ:
    mutex_unlock(&(dev->lock));
    
    return retval;
}

static ssize_t aesd_write(struct file *filp,
                    const char __user *buf, size_t count,
                    loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    struct aesd_dev *dev = filp->private_data;
    mutex_lock(&(dev->lock));
    
    if(count <= 0){
        retval = -EINVAL;
        goto EXIT_WRITE;
    }

    
    char *new_buf = krealloc(dev->partial_buffer.buffptr,
                            dev->partial_buffer.size + count,
                            GFP_KERNEL);
    if(new_buf == NULL){
        retval = -ENOMEM;
        goto EXIT_WRITE;
    }

    unsigned long bytes_not_copied = copy_from_user(&(new_buf[dev->partial_buffer.size]),
    buf,
    count);
    
    retval = count - bytes_not_copied;

    dev->partial_buffer.buffptr = new_buf;
    dev->partial_buffer.size += retval;

    if(buf[count] == '\n'){
        aesd_circular_buffer_add_entry(&(dev->circular_buffer),
                                    &(dev->partial_buffer));

        dev->partial_buffer.buffptr = NULL;
        dev->partial_buffer.size = 0;
        
        
    }

EXIT_WRITE:
    mutex_unlock(&(dev->lock));
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



static int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_device.partial_buffer.buffptr = NULL;
    aesd_device.partial_buffer.size = 0;
    
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.circular_buffer);


    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

static void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    // cleanup circular buffer
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&(aesd_device.circular_buffer),index) {  
        if((entry->buffptr) != NULL){
            kfree(entry->buffptr);
        }
    }

    // destroy mutex
    mutex_destroy(&aesd_device.lock);

    // cleanup partial buffer
    if(aesd_device.partial_buffer.buffptr != NULL){
        kfree(aesd_device.partial_buffer.buffptr);
    }



    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
