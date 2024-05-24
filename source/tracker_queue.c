// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "container_spinlocking.h"
#include "tracker_queue.h"
#include "blk_util.h"

#define SECTION "tracker   "
#include "log_format.h"

static LIST_HEAD(tracker_disk_container);
static DEFINE_SPINLOCK(tracker_disk_container_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)

#ifdef HAVE_MAKE_REQUEST_INT
int tracking_make_request(struct request_queue *q, struct bio *bio);
#else
void tracking_make_request(struct request_queue *q, struct bio *bio);
#endif

#elif defined(VEEAMSNAP_VOID_SUBMIT_BIO)
void tracking_make_request(struct bio* bio);
#elif defined(VEEAMSNAP_DISK_SUBMIT_BIO)
blk_qc_t tracking_make_request(struct bio *bio );
#else
blk_qc_t tracking_make_request( struct request_queue *q, struct bio *bio );
#endif

// find or create new tracker queue
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int tracker_disk_create_or_get(struct gendisk *disk, tracker_disk_t** ptracker_disk)
#else
int tracker_disk_create_or_get(struct request_queue *queue, tracker_disk_t** ptracker_disk)
#endif
{
    int res = SUCCESS;
    tracker_disk_t* tr_disk = NULL;

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    res = tracker_disk_find_and_get(disk, &tr_disk);
#else
    res = tracker_disk_find_and_get(queue, &tr_disk);
#endif
    if ((res < 0) && (-ENODATA != res)){
        log_err_d( "Cannot to find tracker disk. errno=", res );
        return res;
    }
    if (SUCCESS == res) {
        log_tr("Tracker disk already exists");
        *ptracker_disk = tr_disk;
        return res;
    }

    log_tr("New tracker disk create");

    tr_disk = kzalloc(sizeof(tracker_disk_t), GFP_NOIO );
    if (NULL==tr_disk)
        return -ENOMEM;

    INIT_LIST_HEAD(&tr_disk->link);
    kref_init(&tr_disk->kref);

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    if (!ke_get_addr(KE_BLK_MQ_SUBMIT_BIO)) {
        log_err("Kernel entry 'blk_mq_submit_bio' is not initialized.");
        kfree(tr_disk);
        return -ENOSYS;
    }
    log_tr("The disk's fops is being substituted.");
#else
#if defined(VEEAMSNAP_MQ_REQUEST)
    if (!ke_get_addr(KE_BLK_MQ_MAKE_REQUEST)) {
        log_err("Kernel entry 'blk_mq_make_request' is not initialized.");
        kfree(tr_disk);
        return -ENOSYS;
    }
#endif
    log_tr("Tracker disk is being detached.");
#endif

    spin_lock(&tracker_disk_container_lock);
    list_add_tail(&tr_disk->link, &tracker_disk_container);
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    tr_disk->disk = disk;

    /* copy fops from original disk except submit_bio */
    tr_disk->original_fops = (struct block_device_operations *)(disk->fops);
    memcpy(&tr_disk->fops, tr_disk->original_fops, sizeof(struct block_device_operations));
    tr_disk->fops.submit_bio = tracking_make_request;
    barrier();

    /* lock cpu */
    //cant_sleep();
    local_irq_disable();
    barrier();

    {/* change original disks fops */
        unsigned long cr0 = disable_page_protection();
        barrier();

        disk->fops = &tr_disk->fops;
        barrier();

        reenable_page_protection(cr0);
        barrier();
    }

    /* unlock cpu */
    local_irq_enable();
#else
    tr_disk->queue = queue;
    tr_disk->original_make_request_fn = queue->make_request_fn;
    queue->make_request_fn = tracking_make_request;
#endif
    spin_unlock(&tracker_disk_container_lock);

    *ptracker_disk = tr_disk;

    log_tr("New tracker disk was created.");

    return SUCCESS;
}

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int __tracker_disk_find(struct gendisk *disk, tracker_disk_t** ptracker_disk, bool kref)
#else
int __tracker_disk_find(struct request_queue *queue, tracker_disk_t** ptracker_disk, bool kref)
#endif
{
    int result = -ENODATA;

    spin_lock(&tracker_disk_container_lock);
    if (!list_empty(&tracker_disk_container)) {
        tracker_disk_t* tr_disk;

        list_for_each_entry(tr_disk, &tracker_disk_container, link) {
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
            if (tr_disk->disk == disk)
#else
            if (tr_disk->queue == queue)
#endif
            {
                if (kref)
                    kref_get(&tr_disk->kref);
                *ptracker_disk = tr_disk;

                result = SUCCESS;    //don`t continue
                break;
            }
        }
    }
    spin_unlock(&tracker_disk_container_lock);

    return result;
}

void tracker_disk_free(struct kref* kref)
{
    tracker_disk_t* tr_disk = container_of(kref, tracker_disk_t, kref);

    list_del(&tr_disk->link);

    /* lock cpu */
    //cant_sleep();
    local_irq_disable();
    barrier();

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    {/* restore original disks fops */
        unsigned long cr0 = disable_page_protection();
        barrier();

        tr_disk->disk->fops = tr_disk->original_fops;
        barrier();

        reenable_page_protection(cr0);
    }
#else
    tr_disk->queue->make_request_fn = tr_disk->original_make_request_fn;
#endif
    /* unlock cpu */
    local_irq_enable();

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    log_tr("The disk's fops is being restored.");
#else
    log_tr("Tracker disk detached.");
#endif

    kfree(tr_disk);
}

void tracker_disk_put(tracker_disk_t* ptracker_disk)
{
    spin_lock(&tracker_disk_container_lock);
    kref_put(&ptracker_disk->kref, tracker_disk_free);
    spin_unlock(&tracker_disk_container_lock);
}
