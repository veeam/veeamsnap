// Copyright (c) Veeam Software Group GmbH

#pragma once
#include "container_spinlocking.h"
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO) || defined(VEEAMSNAP_MQ_REQUEST)
#include <linux/blk-mq.h>
#endif

#ifdef VEEAMSNAP_DISK_SUBMIT_BIO
#ifdef VEEAMSNAP_VOID_SUBMIT_BIO
typedef void (make_request_fn) (struct bio* bio);
#else
typedef blk_qc_t (make_request_fn) (struct bio *bio);
#endif
#endif

typedef struct _tracker_disk_s
{
    struct list_head link;
    struct kref kref;
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    struct gendisk *disk;
    struct block_device_operations fops;
    struct block_device_operations* original_fops;
#else
    make_request_fn* original_make_request_fn;
    struct request_queue *queue;
#endif
    
}tracker_disk_t;

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int tracker_disk_create_or_get(struct gendisk *disk, tracker_disk_t** ptracker_disk);
int __tracker_disk_find(struct gendisk* disk, tracker_disk_t** ptracker_disk, bool kref);
static inline int tracker_disk_find(struct gendisk* disk, tracker_disk_t** ptracker_disk)
{
    return __tracker_disk_find(disk, ptracker_disk, false);
};
static inline int tracker_disk_find_and_get(struct gendisk* disk, tracker_disk_t** ptracker_disk)
{
    return __tracker_disk_find(disk, ptracker_disk, true);
};
#else
int tracker_disk_create_or_get(struct request_queue *q, tracker_disk_t** ptracker_disk);
int __tracker_disk_find(struct request_queue* q, tracker_disk_t** ptracker_disk, bool kref);
static inline int tracker_disk_find(struct request_queue* q, tracker_disk_t** ptracker_disk)
{
    return __tracker_disk_find(q, ptracker_disk, false);
};
static inline int tracker_disk_find_and_get(struct request_queue* q, tracker_disk_t** ptracker_disk)
{
    return __tracker_disk_find(q, ptracker_disk, true);
};
#endif

void tracker_disk_put(tracker_disk_t* ptracker_disk);


#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
#include "kernel_entries.h"

static inline make_request_fn * tracker_disk_get_original_make_request(tracker_disk_t* tr_disk)
{
    if (tr_disk->original_fops->submit_bio)
        return tr_disk->original_fops->submit_bio;

    /* prevents distortion of q_usage_counter counter in blk_queue_exit() */
    percpu_ref_get(&tr_disk->disk->queue->q_usage_counter);
#ifdef VEEAMSNAP_VOID_SUBMIT_BIO
    return (void(*)(struct bio*))
        ke_get_addr(KE_BLK_MQ_SUBMIT_BIO);
#else
    return (blk_qc_t(*)(struct bio *))
        ke_get_addr(KE_BLK_MQ_SUBMIT_BIO);
#endif
}

#elif defined(VEEAMSNAP_MQ_REQUEST)
#include "kernel_entries.h"
static inline make_request_fn * tracker_disk_get_original_make_request(tracker_disk_t* tr_disk)
{
    if (tr_disk->original_make_request_fn)
        return tr_disk->original_make_request_fn;

    /* prevents distortion of q_usage_counter counter in blk_queue_exit() */
    percpu_ref_get(&tr_disk->queue->q_usage_counter);
#if defined(BLK_MQ_MAKE_REQUEST_EXPORTED)
    return blk_mq_make_request;
#else
    return (blk_qc_t(*)(struct request_queue *, struct bio *))
        ke_get_addr(KE_BLK_MQ_MAKE_REQUEST);
#endif
}

#else

static inline make_request_fn * tracker_disk_get_original_make_request(tracker_disk_t* tr_disk)
{
    return tr_disk->original_make_request_fn;
}

#endif
