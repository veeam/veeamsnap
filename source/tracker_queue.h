// Copyright (c) Veeam Software Group GmbH

#pragma once
#include "container_spinlocking.h"
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO) || defined(VEEAMSNAP_MQ_REQUEST)
#include <linux/blk-mq.h>
#endif

#ifdef VEEAMSNAP_DISK_SUBMIT_BIO
typedef blk_qc_t (make_request_fn) (struct bio *bio);
#endif

typedef struct _tracker_disk_s
{
    content_sl_t content;
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    struct gendisk *disk;
#ifdef HAVE_BLK_INTERPOSER
    struct blk_interposer interposer;
#else
    struct block_device_operations fops;
    struct block_device_operations* original_fops;
#endif
#else
    make_request_fn* original_make_request_fn;
    struct request_queue *queue;
#endif
    atomic_t atomic_ref_count;

}tracker_disk_t;

int tracker_disk_init(void );
int tracker_disk_done(void );

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int tracker_disk_ref(struct gendisk *disk, tracker_disk_t** ptracker_disk);
int tracker_disk_find(struct gendisk *disk, tracker_disk_t** ptracker_disk);
#else
int tracker_disk_ref(struct request_queue *q, tracker_disk_t** ptracker_disk);
int tracker_disk_find(struct request_queue *q, tracker_disk_t** ptracker_disk);
#endif

void tracker_disk_unref(tracker_disk_t* ptracker_disk);

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)

static inline void blk_disk_freeze(struct gendisk *disk)
{
    blk_mq_freeze_queue(disk->queue);
    blk_mq_quiesce_queue(disk->queue);
}
static inline void blk_disk_unfreeze(struct gendisk *disk)
{
    blk_mq_unquiesce_queue(disk->queue);
    blk_mq_unfreeze_queue(disk->queue);
}

#endif

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)

static inline make_request_fn * tracker_disk_get_original_make_request(tracker_disk_t* tr_disk)
{
    if (tr_disk->original_fops->submit_bio)
        return tr_disk->original_fops->submit_bio;

    /* prevents distortion of q_usage_counter counter in blk_queue_exit() */
    percpu_ref_get(&tr_disk->disk->queue->q_usage_counter);
    return (blk_qc_t (*)(struct bio *))
        (BLK_MQ_SUBMIT_BIO_ADDR + (long long)(((void *)printk) - (void *)PRINTK_ADDR));
}

#elif defined(VEEAMSNAP_MQ_REQUEST)

static inline make_request_fn * tracker_disk_get_original_make_request(tracker_disk_t* tr_disk)
{
    if (tr_disk->original_make_request_fn)
        return tr_disk->original_make_request_fn;

    /* prevents distortion of q_usage_counter counter in blk_queue_exit() */
    percpu_ref_get(&tr_disk->queue->q_usage_counter);
#if defined(BLK_MQ_MAKE_REQUEST_EXPORTED)
    return blk_mq_make_request;
#else
    return (blk_qc_t (*)(struct request_queue *, struct bio *))
        (BLK_MQ_MAKE_REQUEST_ADDR + (long long)(((void *)printk) - (void *)PRINTK_ADDR));
#endif
}

#else

static inline make_request_fn * tracker_disk_get_original_make_request(tracker_disk_t* tr_disk)
{
    return tr_disk->original_make_request_fn;
}

#endif
