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
