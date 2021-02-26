// Copyright (c) Veeam Software Group GmbH

#pragma once
#include "container_spinlocking.h"

typedef struct _tracker_queue_s
{
    content_sl_t content;
#ifdef HAVE_BLK_INTERPOSER
    struct blk_interposer interposer;
    struct gendisk *disk;
#else
    struct request_queue* original_queue;
    make_request_fn* original_make_request_fn;
#endif
    atomic_t atomic_ref_count;

}tracker_queue_t;

int tracker_queue_init(void );
int tracker_queue_done(void );

#ifdef HAVE_BLK_INTERPOSER
int tracker_queue_ref(struct gendisk *disk, tracker_queue_t** ptracker_queue);
int tracker_queue_find(struct gendisk *disk, tracker_queue_t** ptracker_queue);
#else //HAVE_BLK_INTERPOSER
int tracker_queue_ref(struct request_queue* queue, tracker_queue_t** ptracker_queue );
int tracker_queue_find(struct request_queue* queue, tracker_queue_t** ptracker_queue);
#endif //HAVE_BLK_INTERPOSER

void tracker_queue_unref(tracker_queue_t* ptracker_queue);

#ifdef HAVE_BLK_INTERPOSER
#include <linux/blk-mq.h>
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
#else
#ifdef VEEAMSNAP_MQ_REQUEST

#include <linux/blk-mq.h>

static inline make_request_fn * tracker_queue_get_original_make_request(tracker_queue_t* tracker_queue)
{
    struct request_queue *q;

    if (tracker_queue->original_make_request_fn)
        return tracker_queue->original_make_request_fn;

    /* prevents distortion of q_usage_counter counter in blk_queue_exit() */
    q = tracker_queue->original_queue;
    percpu_ref_get(&q->q_usage_counter);

    return blk_mq_make_request;
}
#else
static inline make_request_fn * tracker_queue_get_original_make_request(tracker_queue_t* tracker_queue)
{
    return tracker_queue->original_make_request_fn;
}
#endif
#endif
