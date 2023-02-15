// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "container_spinlocking.h"
#include "tracker_queue.h"
#include "blk_util.h"

#define SECTION "tracker   "
#include "log_format.h"

container_sl_t tracker_disk_container;

int tracker_disk_init(void)
{
    container_sl_init(&tracker_disk_container, sizeof(tracker_disk_t));
    return SUCCESS;
}

void tracker_disk_done(void)
{
    content_sl_t* content;

    while (NULL != (content = container_sl_first( &tracker_disk_container ))){
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
        struct gendisk* disk = ((tracker_disk_t*)content)->disk;

        blk_mq_freeze_queue(disk->queue);
#endif
        container_sl_free(content);
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
        blk_mq_unfreeze_queue(disk->queue);
#endif
    }

    if (SUCCESS != container_sl_done(&tracker_disk_container))
        log_err("Failed to free up tracker queue container");
    return ;
}

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
int tracker_disk_ref(struct gendisk *disk, tracker_disk_t** ptracker_disk)
#else
int tracker_disk_ref(struct request_queue *queue, tracker_disk_t** ptracker_disk)
#endif
{
    int res = SUCCESS;
    tracker_disk_t* tr_disk = NULL;

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    res = tracker_disk_find(disk, &tr_disk);
#else
    res = tracker_disk_find(queue, &tr_disk);
#endif
    if ((res < 0) && (-ENODATA != res)){
        log_err_d( "Cannot to find tracker disk. errno=", res );
        return res;
    }
    if ((SUCCESS == res) && atomic_read(&tr_disk->atomic_ref_count)) {
        log_tr("Tracker disk already exists");

        *ptracker_disk = tr_disk;
        atomic_inc( &tr_disk->atomic_ref_count );

        return res;
    }

    if (-ENODATA == res){
        log_tr("New tracker disk create");

        tr_disk = (tracker_disk_t*)container_sl_new(&tracker_disk_container);
        if (NULL==tr_disk)
            return -ENOMEM;

        atomic_set( &tr_disk->atomic_ref_count, 0 );

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
        log_tr("The disk's fops is being substituted.");

        if (!ke_get_addr(KE_BLK_MQ_SUBMIT_BIO)) {
            log_err("TBD: Kernel entry 'blk_mq_submit_bio' is not initialized.");
            container_sl_free(&tr_disk->content);
            return -ENOSYS;
        }

        /* copy fops from original disk except submit_bio */
        tr_disk->original_fops = (struct block_device_operations *)(disk->fops);
        memcpy(&tr_disk->fops, tr_disk->original_fops, sizeof(struct block_device_operations));
        tr_disk->fops.submit_bio = tracking_make_request;
        barrier();
    }

    /* lock cpu */
    preempt_disable();
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
    preempt_enable();

    tr_disk->disk = disk;
#else

#if defined(VEEAMSNAP_MQ_REQUEST)
        if (!ke_get_addr(KE_BLK_MQ_MAKE_REQUEST)) {
            log_err("TBD: Kernel entry 'blk_mq_make_request' is not initialized.");
            container_sl_free(&tr_disk->content);
            return -ENOSYS;
        }
#endif
        tr_disk->original_make_request_fn = queue->make_request_fn;
    }
    queue->make_request_fn = tracking_make_request;

    tr_disk->queue = queue;
#endif

    *ptracker_disk = tr_disk;
    atomic_inc( &tr_disk->atomic_ref_count );

    log_tr("New tracker disk was created");

    return SUCCESS;
}
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int tracker_disk_find( struct gendisk *disk, tracker_disk_t** ptracker_disk )
#else
int tracker_disk_find( struct request_queue *queue, tracker_disk_t** ptracker_disk )
#endif
{
    int result = -ENODATA;
    content_sl_t* pContent = NULL;
    tracker_disk_t* tr_disk = NULL;

    CONTAINER_SL_FOREACH_BEGIN( tracker_disk_container, pContent )
    {
        tr_disk = (tracker_disk_t*)pContent;
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
        if (tr_disk->disk == disk)
#else
        if (tr_disk->queue == queue)
#endif
        {
            *ptracker_disk = tr_disk;

            result = SUCCESS;    //don`t continue
            break;
        }
    }CONTAINER_SL_FOREACH_END( tracker_disk_container );

    return result;
}

void tracker_disk_unref(tracker_disk_t* tr_disk)
{
    if (atomic_dec_and_test(&tr_disk->atomic_ref_count)) {
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
        struct gendisk *disk = tr_disk->disk;

        log_tr("The disk's fops is being restored.");

        /* lock cpu */
        preempt_disable();
        local_irq_disable();
        barrier();

        {/* restore original disks fops */
            unsigned long cr0 = disable_page_protection();
            barrier();

            disk->fops = tr_disk->original_fops;
            barrier();

            reenable_page_protection(cr0);
        }

        /* unlock cpu */
        local_irq_enable();
        preempt_enable();
#else
        tr_disk->queue->make_request_fn = tr_disk->original_make_request_fn;
#endif

        log_tr("Tracker disk detached");
    }
    else
        log_tr("Tracker disk is in use");
}
