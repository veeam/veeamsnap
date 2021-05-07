// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "tracking.h"

#include "tracker.h"
#include "tracker_queue.h"
#include "snapdata_collect.h"
#include "blk_util.h"
#include "blk_direct.h"
#include "defer_io.h"
#include "cbt_persistent.h"

#define SECTION "tracking  "
#include "log_format.h"

#ifdef HAVE_BLK_INTERPOSER
/**
 * tracking_submit_bio() - Intercept bio by blk_interposer
 */
bool tracking_submit_bio(struct bio *bio)
{
    bool was_catched = false;

    tracker_disk_t* tr_disk = NULL;
    snapdata_collector_t* collector = NULL;
    tracker_t* tracker = NULL;

    bio_get(bio);
    tr_disk = container_of(bio->bi_disk->interposer, tracker_disk_t, interposer);

    if (SUCCESS == tracker_find_by_queue_and_sector(tr_disk, bio_bi_sector(bio), &tracker)) {
        sector_t sectStart = (bio_bi_sector(bio) - blk_dev_get_start_sect( tracker->target_dev ));
        sector_t sectCount = sector_from_size( bio_bi_size(bio) );

        if (op_is_write(bio_op(bio))) {// only write request processed
            if (SUCCESS == snapdata_collect_Find(bio, &collector))
                snapdata_collect_Process(collector, bio);
        }

        if ((bio->bi_end_io != blk_direct_bio_endio) &&
            (bio->bi_end_io != blk_redirect_bio_endio) &&
            (bio->bi_end_io != blk_deferred_bio_endio)) {

            if (tracker->is_unfreezable)
                down_read(&tracker->unfreezable_lock);

            if (atomic_read(&tracker->is_captured)) {
                // do copy-on-write
                int res = defer_io_redirect_bio(tracker->defer_io, bio, sectStart, sectCount, tracker);
                if (SUCCESS == res)
                    was_catched = true;
            }

            if (tracker->is_unfreezable)
                up_read(&tracker->unfreezable_lock);
        }

        if (!was_catched) {
            bool cbt_locked = false;

            if (tracker && bio_data_dir(bio) && bio_has_data(bio)) {
                cbt_locked = tracker_cbt_bitmap_lock(tracker);
                if (cbt_locked)
                    tracker_cbt_bitmap_set(tracker, sectStart, sectCount);
            }
            if (cbt_locked)
                tracker_cbt_bitmap_unlock(tracker);
        }
    }

    bio_put(bio);

    return was_catched;
}

#else //HAVE_BLK_INTERPOSER

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)

#ifdef HAVE_MAKE_REQUEST_INT
int tracking_make_request(struct request_queue *q, struct bio *bio)
{
#else
void tracking_make_request(struct request_queue *q, struct bio *bio)
{
#endif

#else

#ifdef VEEAMSNAP_DISK_SUBMIT_BIO
blk_qc_t tracking_make_request(struct bio *bio )
{
#else
blk_qc_t tracking_make_request( struct request_queue *q, struct bio *bio )
{
#endif
    blk_qc_t result = BLK_QC_T_NONE;
#endif

    sector_t bi_sector;
    unsigned int bi_size;
    tracker_disk_t* tr_disk = NULL;
    snapdata_collector_t* collector = NULL;
    tracker_t* tracker = NULL;

    bio_get(bio);

    if (SUCCESS == tracker_disk_find(bio->bi_disk, &tr_disk)){
#ifndef REQ_OP_BITS
        if ( bio->bi_rw & WRITE ){// only write request processed
#else
        if ( op_is_write( bio_op( bio ) ) ){// only write request processed
#endif
            if (SUCCESS == snapdata_collect_Find( bio, &collector ))
                snapdata_collect_Process( collector, bio );
        }

        bi_sector = bio_bi_sector( bio );
        bi_size = bio_bi_size(bio);

        if (SUCCESS == tracker_find_by_queue_and_sector( tr_disk, bi_sector, &tracker )){
            sector_t sectStart = (bi_sector - blk_dev_get_start_sect( tracker->target_dev ));
            sector_t sectCount = sector_from_size( bi_size );

            if ((bio->bi_end_io != blk_direct_bio_endio) &&
                (bio->bi_end_io != blk_redirect_bio_endio) &&
                (bio->bi_end_io != blk_deferred_bio_endio))
            {
                bool do_lowlevel = true;

                if ((sectStart + sectCount) > blk_dev_get_capacity( tracker->target_dev ))
                    sectCount -= ((sectStart + sectCount) - blk_dev_get_capacity( tracker->target_dev ));


                if (tracker->is_unfreezable)
                    down_read(&tracker->unfreezable_lock);

                if (atomic_read( &tracker->is_captured ))
                {// do copy-on-write
                    int res = defer_io_redirect_bio( tracker->defer_io, bio, sectStart, sectCount, tracker_disk_get_original_make_request(tr_disk), tracker );
                    if (SUCCESS == res)
                        do_lowlevel = false;
                }

                if (tracker->is_unfreezable)
                    up_read(&tracker->unfreezable_lock);

                if (do_lowlevel){
                    bool cbt_locked = false;

                    if (tracker && bio_data_dir( bio ) && bio_has_data( bio )){
                        cbt_locked = tracker_cbt_bitmap_lock( tracker );
                        if (cbt_locked)
                            tracker_cbt_bitmap_set( tracker, sectStart, sectCount );
                        //tracker_CbtBitmapUnlock( tracker );
                    }
                    //call low level block device
                    {
                        make_request_fn* fn = tracker_disk_get_original_make_request(tr_disk);

                        if (fn) {
#ifdef VEEAMSNAP_DISK_SUBMIT_BIO
                            fn(bio);
#else
                            fn(bio->bi_disk->queue, bio);
#endif
                        } else {
                            log_err("Invalid original bio processing function");
                            bio_io_error(bio);
                        }
                    }

                    if (cbt_locked)
                        tracker_cbt_bitmap_unlock( tracker );
                }
            }
            else
            {
                bool cbt_locked = false;

                if (tracker && bio_data_dir( bio ) && bio_has_data( bio )){
                    cbt_locked = tracker_cbt_bitmap_lock( tracker );
                    if (cbt_locked)
                        tracker_cbt_bitmap_set( tracker, sectStart, sectCount );
                }
                {
                    make_request_fn* fn = tracker_disk_get_original_make_request(tr_disk);

                    if (fn) {
#ifdef VEEAMSNAP_DISK_SUBMIT_BIO
                        fn(bio);
#else
                        fn(bio->bi_disk->queue, bio);
#endif
                    } else {
                        log_err("Invalid original bio processing function");
                        bio_io_error(bio);
                    }
                }
                if (cbt_locked)
                    tracker_cbt_bitmap_unlock( tracker );
            }
        }else{
            //call low level block device
            make_request_fn* fn = tracker_disk_get_original_make_request(tr_disk);

            if (fn) {
#ifdef VEEAMSNAP_DISK_SUBMIT_BIO
                fn(bio);
#else
                fn(bio->bi_disk->queue, bio);
#endif
            } else {
                log_err("Invalid original bio processing function");
                bio_io_error(bio);
            }
        }

    }else {
        log_err("CRITICAL! Cannot find tracker disk");
        bio_io_error(bio);
    }

    bio_put(bio);

#if  LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)

#ifdef HAVE_MAKE_REQUEST_INT
    return 0;
#endif

#else
    return result;
#endif
}

#endif //HAVE_BLK_INTERPOSER

int tracking_add(dev_t dev_id, unsigned int cbt_block_size_degree, unsigned long long snapshot_id)
{
    int result = SUCCESS;
    tracker_t* tracker = NULL;

    log_tr_format( "Adding device [%d:%d] under tracking", MAJOR(dev_id), MINOR(dev_id) );

    result = tracker_find_by_dev_id( dev_id, &tracker );
    if (SUCCESS == result){
        log_tr_format( "Device [%d:%d] is already tracked", MAJOR( dev_id ), MINOR( dev_id ) );

        if ((snapshot_id != 0ull) && (tracker_snapshot_id_get(tracker) == 0ull))
            tracker_snapshot_id_set(tracker, snapshot_id);

        if (NULL == tracker->cbt_map){
            cbt_map_t* cbt_map = cbt_map_create((cbt_block_size_degree-SECTOR_SHIFT), blk_dev_get_capacity(tracker->target_dev));
            if (cbt_map == NULL){
                result = -ENOMEM;
            }
            else{
                tracker_cbt_start(tracker, snapshot_id, cbt_map);
#ifdef PERSISTENT_CBT
                cbt_persistent_register(tracker->original_dev_id, tracker->cbt_map);
#endif
                result = -EALREADY;
            }
        }
        else{
            bool reset_needed = false;
            if (!tracker->cbt_map->active){
                reset_needed = true;
                log_warn( "Nonactive CBT table detected. CBT fault" );
            }

            if (tracker->cbt_map->device_capacity != blk_dev_get_capacity( tracker->target_dev )){
                reset_needed = true;
                log_warn( "Device resize detected. CBT fault" );
            }

            if (reset_needed)
            {
                result = tracker_remove( tracker );
                if (SUCCESS != result){
                    log_err_d( "Failed to remove tracker. errno=", result );
                }
                else{
                    result = tracker_create(snapshot_id, dev_id, cbt_block_size_degree, NULL, &tracker);
                    if (SUCCESS != result){
                        log_err_d( "Failed to create tracker. errno=", result );
                    }
                }
            }
            if (result == SUCCESS)
                result = -EALREADY;
        }
    }
    else if (-ENODATA == result)
    {
        struct block_device* target_dev = NULL;
        tracker_disk_t* tr_disk = NULL;

        do {//check space already under tracking
            sector_t sectStart;
            sector_t sectEnd;

            result = blk_dev_open(dev_id, &target_dev);
            if (result != SUCCESS)
                break;

            sectStart = blk_dev_get_start_sect(target_dev);
            sectEnd = blk_dev_get_capacity(target_dev) + sectStart;

            if (SUCCESS == tracker_disk_find(target_dev->bd_disk, &tr_disk)) {
                tracker_t* old_tracker = NULL;

                if (SUCCESS == tracker_find_intersection(tr_disk, sectStart, sectEnd, &old_tracker)){
                    log_warn_format("Removing the device [%d:%d] from tracking",
                        MAJOR(old_tracker->original_dev_id), MINOR(old_tracker->original_dev_id));

                    result = tracker_remove(old_tracker);
                    if (SUCCESS != result){
                        log_err_d("Failed to remove the old tracker. errno=", result);
                        break;
                    }
                }
            }

            result = tracker_create(snapshot_id, dev_id, cbt_block_size_degree, NULL, &tracker);
            if (SUCCESS != result)
                log_err_d("Failed to create tracker. errno=", result);
            else
            {
                char dev_name[BDEVNAME_SIZE + 1];
                memset(dev_name, 0, BDEVNAME_SIZE + 1);
                if (bdevname(target_dev, dev_name))
                    log_tr_s("Add to tracking device ", dev_name);
/*
                if (target_dev->bd_part && target_dev->bd_part->info){
                    if (target_dev->bd_part->info->uuid)
                        log_tr_s("partition uuid: ", target_dev->bd_part->info->uuid);
                    if (target_dev->bd_part->info->volname)
                        log_tr_s("volume name: ", target_dev->bd_part->info->volname);
                }
*/
                if (target_dev->bd_super){
                    log_tr_s("fs id: ", target_dev->bd_super->s_id);
                }
                else
                    log_tr("fs not found");
            }
        } while (false);

        if (target_dev)
            blk_dev_close(target_dev);
    }
    else
        log_err_format( "Unable to add device [%d:%d] under tracking: invalid trackers container. errno=%d",
            MAJOR( dev_id ), MINOR( dev_id ), result );


    return result;
}


int tracking_remove( dev_t dev_id )
{
    int result = SUCCESS;
    tracker_t* tracker = NULL;

    log_tr_format( "Removing device [%d:%d] from tracking", MAJOR(dev_id), MINOR(dev_id) );

    result = tracker_find_by_dev_id(dev_id, &tracker);
    if ( SUCCESS == result ){
        if ((tracker) && (tracker_snapshot_id_get(tracker) == 0ull)){
            result = tracker_remove( tracker );
            if (SUCCESS == result)
                tracker = NULL;
            else
                log_err_d("Unable to remove device from tracking: failed to remove tracker. errno=", result);
        }
        else{
            log_err_format("Unable to remove device from tracking: snapshot [0x%llx] is created ", tracker_snapshot_id_get(tracker));
            result = -EBUSY;
        }
    }else if (-ENODATA == result)
        log_err_format( "Unable to remove device [%d:%d] from tracking: tracker not found", MAJOR(dev_id), MINOR(dev_id) );
    else
        log_err_format( "Unable to remove device [%d:%d] from tracking: invalid trackers container. errno=",
            MAJOR( dev_id ), MINOR( dev_id ), result );

#ifdef PERSISTENT_CBT
    cbt_persistent_unregister(dev_id);
#endif

    return result;
}


int tracking_collect( int max_count, struct cbt_info_s* p_cbt_info, int* p_count )
{
    int res = tracker_enum_cbt_info( max_count, p_cbt_info, p_count );

    if (res == SUCCESS){
        log_tr_format("%d devices found under tracking", *p_count);
        //if ((p_cbt_info != NULL) && (*p_count < 8)){
        //    size_t inx;
        //    for (inx = 0; inx < *p_count; ++inx){
        //        log_tr_format("\tdevice [%d:%d], snapshot number %d, cbt map size %d bytes",
        //            p_cbt_info[inx].dev_id.major, p_cbt_info[inx].dev_id.minor, p_cbt_info[inx].snap_number, p_cbt_info[inx].cbt_map_size);
        //    }
        //}
    }
    else if (res == -ENODATA){
        log_tr( "There are no devices under tracking" );
        *p_count = 0;
        res = SUCCESS;
    }else
        log_err_d( "Failed to collect devices under tracking. errno=", res );

    return res;
}


int tracking_read_cbt_bitmap( dev_t dev_id, unsigned int offset, size_t length, void __user* user_buff )
{
    int result = SUCCESS;
    tracker_t* tracker = NULL;

    result = tracker_find_by_dev_id(dev_id, &tracker);
    if ( SUCCESS == result ){
        if (atomic_read( &tracker->is_captured )){
            result = cbt_map_read_to_user( tracker->cbt_map, user_buff, offset, length );
        }
        else{
            log_err_format( "Unable to read CBT bitmap for device [%d:%d]: device is not captured by snapshot", MAJOR(dev_id), MINOR(dev_id) );
            result = -EPERM;
        }
    }else if (-ENODATA == result)
        log_err_format( "Unable to read CBT bitmap for device [%d:%d]: device not found", MAJOR( dev_id ), MINOR( dev_id ) );
    else
        log_err_d( "Failed to find devices under tracking. errno=", result );

    return result;
}
