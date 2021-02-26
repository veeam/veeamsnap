// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "container_spinlocking.h"
#include "tracker_queue.h"

#define SECTION "tracker   "

container_sl_t tracker_queue_container;

#ifdef HAVE_BLK_INTERPOSER
bool tracking_submit_bio(struct bio *bio);

static void submit_bio_interposer_fn(struct bio *bio)
{
    if (tracking_submit_bio(bio))
        return;

    /* add bio to bio_list then bio was not intercepted */
    BUG_ON(!current->bio_list);
    bio_list_add(&current->bio_list[0], bio);
}
#endif

int tracker_queue_init(void)
{
    container_sl_init(&tracker_queue_container, sizeof(tracker_queue_t));
    return SUCCESS;
}

int tracker_queue_done(void)
{
    int result = container_sl_done(&tracker_queue_container);
    if (SUCCESS != result)
        log_err("Failed to free up tracker queue container");
    return result;
}

#ifdef HAVE_BLK_INTERPOSER

// find or create new tracker queue
int tracker_queue_ref(struct gendisk *disk, tracker_queue_t** ptracker_queue)
{
    int ret = SUCCESS;
    tracker_queue_t* tr_q = NULL;

    ret = tracker_queue_find(disk, &tr_q);
    if (SUCCESS == ret) {
        log_tr("Tracker queue already exists");

        *ptracker_queue = tr_q;
        atomic_inc(&tr_q->atomic_ref_count);

        return ret;
    }

    if (-ENODATA != ret) {
        log_err_d("Cannot to find tracker queue. errno=", ret);
        return ret;
    }

    log_tr("New tracker queue create");

    tr_q = (tracker_queue_t*)content_sl_new(&tracker_queue_container);
    if (NULL == tr_q)
        return -ENOMEM;

    atomic_set(&tr_q->atomic_ref_count, 0);
    tr_q->disk = disk;

    *ptracker_queue = tr_q;
    atomic_inc(&tr_q->atomic_ref_count);

    blk_disk_freeze(disk);
    {
        ret = blk_interposer_attach(disk, &tr_q->interposer, submit_bio_interposer_fn);
        if (unlikely(ret))
            log_err_d("Failed to attack blk_interposer. errno=", ret);
        else
            container_sl_push_back(&tracker_queue_container, &tr_q->content);
    }
    blk_disk_unfreeze(disk);

    if (ret) {
        log_err("Failed to attach interposer to disk");
        content_sl_free(&tr_q->content);
        return ret;
    }

    log_tr("New tracker queue was created");
    return SUCCESS;
}

int tracker_queue_find(struct gendisk *disk, tracker_queue_t** ptracker_queue)
{
    int result = -ENODATA;
    content_sl_t* pContent = NULL;
    tracker_queue_t* tr_q = NULL;

    CONTAINER_SL_FOREACH_BEGIN(tracker_queue_container, pContent)
    {
        tr_q = (tracker_queue_t*)pContent;
        if ((tr_q->disk == disk)) {
            *ptracker_queue = tr_q;

            result = SUCCESS;    //don`t continue
            break;
        }
    }CONTAINER_SL_FOREACH_END(tracker_queue_container);

    return result;
}

#else //HAVE_BLK_INTERPOSER

#if  LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)

#ifdef HAVE_MAKE_REQUEST_INT
int tracking_make_request(struct request_queue *q, struct bio *bio);
#else
void tracking_make_request(struct request_queue *q, struct bio *bio);
#endif

#else
blk_qc_t tracking_make_request( struct request_queue *q, struct bio *bio );
#endif


// find or create new tracker queue
int tracker_queue_ref(struct request_queue* queue, tracker_queue_t** ptracker_queue)
{
    int find_result = SUCCESS;
    tracker_queue_t* tr_q = NULL;

    find_result = tracker_queue_find(queue, &tr_q);
    if (SUCCESS == find_result){
        log_tr("Tracker queue already exists");

        *ptracker_queue = tr_q;
        atomic_inc( &tr_q->atomic_ref_count );

        return find_result;
    }

    if (-ENODATA != find_result){
        log_err_d( "Cannot to find tracker queue. errno=", find_result );
        return find_result;
    }

    log_tr("New tracker queue create" );

    tr_q = (tracker_queue_t*)container_sl_new(&tracker_queue_container);
    if (NULL==tr_q)
        return -ENOMEM;

    atomic_set( &tr_q->atomic_ref_count, 0 );

    tr_q->original_make_request_fn = queue->make_request_fn;
    queue->make_request_fn = tracking_make_request;

    tr_q->original_queue = queue;

    *ptracker_queue = tr_q;
    atomic_inc( &tr_q->atomic_ref_count );

    log_tr("New tracker queue was created");

    return SUCCESS;
}

int tracker_queue_find( struct request_queue* queue, tracker_queue_t** ptracker_queue )
{
    int result = -ENODATA;
    content_sl_t* pContent = NULL;
    tracker_queue_t* tr_q = NULL;
    CONTAINER_SL_FOREACH_BEGIN( tracker_queue_container, pContent )
    {
        tr_q = (tracker_queue_t*)pContent;
        if (tr_q->original_queue == queue){
            *ptracker_queue = tr_q;

            result = SUCCESS;    //don`t continue
            break;
        }
    }CONTAINER_SL_FOREACH_END( tracker_queue_container );

    return result;
}
#endif //HAVE_BLK_INTERPOSER

void tracker_queue_unref(tracker_queue_t* tracker_queue)
{
    if (atomic_dec_and_test(&tracker_queue->atomic_ref_count)) {
#ifdef HAVE_BLK_INTERPOSER
        struct gendisk *disk = tracker_queue->disk;

        blk_disk_freeze(disk);
        {
            blk_interposer_detach(&tracker_queue->interposer, submit_bio_interposer_fn);
            tracker_queue->disk = NULL;
        }
        blk_disk_unfreeze(disk);
#else
        tracker_queue->original_queue->make_request_fn = tracker_queue->original_make_request_fn;
#endif
        container_sl_free(&tracker_queue->content);

        log_tr("Tracker queue freed");
    }
    else
        log_tr("Tracker queue is in use");
}
