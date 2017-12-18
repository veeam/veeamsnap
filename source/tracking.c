#include "stdafx.h"
#include "tracking.h"

#include "tracker.h"
#include "tracker_queue.h"
#include "snapdata_collect.h"
#include "blk_util.h"
#include "blk_direct.h"
#include "defer_io.h"


#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)

#ifdef HAVE_MAKE_REQUEST_INT
int tracking_make_request(struct request_queue *q, struct bio *bio)
#else
void tracking_make_request(struct request_queue *q, struct bio *bio)
#endif

#else
blk_qc_t tracking_make_request( struct request_queue *q, struct bio *bio )
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	blk_qc_t result = 0;
#endif
	sector_t bi_sector;
	unsigned int bi_size;
	tracker_queue_t* pTrackerQueue = NULL;
	snapdata_collector_t* collector = NULL;
	tracker_t* pTracker = NULL;

	bio_get(bio);

	if (SUCCESS == tracker_queue_Find(q, &pTrackerQueue)){
		//find tracker by queue

#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
		if ( bio->bi_rw & WRITE ){// only write request processed
#else
		if ( op_is_write( bio_op( bio ) ) ){// only write request processed
#endif
			if (SUCCESS == snapdata_collect_Find( q, bio, &collector ))
				snapdata_collect_Process( collector, bio );
		}

		bi_sector = bio_bi_sector( bio );
		bi_size = bio_bi_size(bio);

		if (SUCCESS == tracker_FindByQueueAndSector( pTrackerQueue, bi_sector, &pTracker )){
			sector_t sectStart = 0;
			sector_t sectCount = 0;

			sectStart = (bi_sector - blk_dev_get_start_sect( pTracker->pTargetDev ));
			sectCount = sector_from_size( bi_size );

			if ((bio->bi_end_io != blk_direct_bio_endio) &&
				(bio->bi_end_io != blk_redirect_bio_endio) &&
				(bio->bi_end_io != blk_deferred_bio_endio))
			{
				bool do_lowlevel = true;

				if ((sectStart + sectCount) > blk_dev_get_capacity( pTracker->pTargetDev ))
					sectCount -= ((sectStart + sectCount) - blk_dev_get_capacity( pTracker->pTargetDev ));

				if (pTracker->defer_io != NULL){
					down_read( &pTracker->defer_io->flush_lock );
				}

				if (atomic_read( &pTracker->Freezed )){
					int res = defer_io_redirect_bio( pTracker->defer_io, bio, sectStart, sectCount, q, pTrackerQueue->TargetMakeRequest_fn, pTracker );
					if (SUCCESS == res)
						do_lowlevel = false;
				}
				if (pTracker->defer_io != NULL){
					up_read( &pTracker->defer_io->flush_lock );
				}

				if (do_lowlevel){
					bool cbt_locked = false;

					if (pTracker->underChangeTracking && bio_data_dir( bio ) && bio_has_data( bio )){
						cbt_locked = tracker_CbtBitmapLock( pTracker );
						if (cbt_locked)
							tracker_CbtBitmapSet( pTracker, sectStart, sectCount );
						//tracker_CbtBitmapUnlock( pTracker );
					}
					//call low level block device
					pTrackerQueue->TargetMakeRequest_fn( q, bio );
					if (cbt_locked)
						tracker_CbtBitmapUnlock( pTracker );
				}
			}
			else
			{
				bool cbt_locked = false;

				if (pTracker->underChangeTracking && bio_data_dir( bio ) && bio_has_data( bio )){
					cbt_locked = tracker_CbtBitmapLock( pTracker );
					if (cbt_locked)
						tracker_CbtBitmapSet( pTracker, sectStart, sectCount );
				}
				pTrackerQueue->TargetMakeRequest_fn( q, bio );
				if (cbt_locked)
					tracker_CbtBitmapUnlock( pTracker );
			}
		}else{
			//call low level block device
			pTrackerQueue->TargetMakeRequest_fn(q, bio);
		}

	}else{
		log_errorln_p("CRITICAL! Cannot find queue 0x", q);
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


int tracking_add( dev_t dev_id, unsigned int cbt_block_size_degree )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	log_traceln_dev_t( "Adding. dev_id=", dev_id );

	result = tracker_FindByDevId( dev_id, &pTracker );
	if (SUCCESS == result){
		log_traceln_dev_t( "Device already tracking. Device=", dev_id );

		if (NULL == pTracker->cbt_map){
			tracker_cbt_start( pTracker, 0ULL, cbt_block_size_degree, blk_dev_get_capacity( pTracker->pTargetDev ) );
			result = -EALREADY;
		}
		else{
			bool reset_needed = false;
			if (!pTracker->cbt_map->active){
				reset_needed = true;
				log_warnln( "Nonactive CBT table found. CBT fault." );
			}

			if (pTracker->device_capacity != blk_dev_get_capacity( pTracker->pTargetDev )){
				reset_needed = true;
				log_warnln( "Device resize found. CBT fault." );
			}

			if (reset_needed)
			{
				result = tracker_Remove( pTracker );
				if (SUCCESS != result){
					log_errorln_d( "Failed to remove tracker. error=", result );
				}
				else{
					result = tracker_Create( 0ULL, dev_id, cbt_block_size_degree, &pTracker );
					if (SUCCESS != result){
						log_errorln_d( "Failed to create tracker. error=", result );
					}
				}
			}
			if (result == SUCCESS)
				result = -EALREADY;
		}
	}
	else if (-ENODATA == result){

		result = tracker_Create( 0ULL, dev_id, cbt_block_size_degree, &pTracker );
		if (SUCCESS != result){
			log_errorln_d( "Failed to create tracker. error=", result );
		}
	}
	else{
		log_errorln_dev_t( "Container access fail. Device=", dev_id );
		log_errorln_d( "Error =", result );
	}

	return result;
}


int tracking_remove( dev_t dev_id )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	log_traceln_dev_t( "Removing. dev_id=", dev_id );

	result = tracker_FindByDevId(dev_id, &pTracker);
	if ( SUCCESS == result ){

		if ((pTracker->underChangeTracking) && (pTracker->snapshot_id == 0)){
			result = tracker_Remove( pTracker );
			if (SUCCESS != result){
				log_errorln_d( "Failed to remove tracker. code=", result );
			}
		}
		else{
			log_errorln_llx( "Cannot remove device from change tracking. Snapshot created =", pTracker->snapshot_id );
			result = -EBUSY;
		}
	}else if (-ENODATA == result){
		log_errorln_dev_t( "Cannot find tracker for device=", dev_id );
	}else{
		log_errorln_dev_t( "Container access fail. Device=", dev_id );
		log_errorln_d("Error =", result);
	}

	return result;
}


int tracking_collect( int max_count, struct cbt_info_s* p_cbt_info, int* p_count )
{
	int res = tracker_EnumCbtInfo( max_count, p_cbt_info, p_count );

	if (res == SUCCESS){
		size_t inx;
		for (inx = 0; inx < *p_count; ++inx){

			log_traceln_dev_id_s( "dev_id=", p_cbt_info[inx].dev_id );
			log_traceln_d( "snap_number=", (int)p_cbt_info[inx].snap_number );
			log_traceln_d( "cbt_map_size=", (int)p_cbt_info[inx].cbt_map_size );
		}
	}
	else if (res == -ENODATA){
		log_traceln( "Have not device under CBT." );
		*p_count = 0;
		res = SUCCESS;
	}else{
		log_errorln_d( "tracker_EnumCbtInfo failed. res=", res );
	}

	return res;
}


int tracking_read_cbt_bitmap( dev_t dev_id, unsigned int offset, size_t length, void *user_buff )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	if (!access_ok( VERIFY_WRITE, (void*)user_buff, length )){
		log_errorln( "Invalid buffer" );
		result = -EINVAL;
	}

	result = tracker_FindByDevId(dev_id, &pTracker);
	if ( SUCCESS == result ){
		if (atomic_read( &pTracker->Freezed )){
			if (pTracker->underChangeTracking){
				result = cbt_map_read_to_user( pTracker->cbt_map, user_buff, offset, length );
			}
			else{
				log_errorln_dev_t( "Device is not under change tracking. dev_id=", (int)dev_id );
				result = -ENODATA;
			}
		}
		else{
			log_errorln_dev_t( "Device is not freezed. dev_id=", (int)dev_id );
			result = -EPERM;
		}
	}else if (-ENODATA == result){
		log_errorln_dev_t( "Cannot find tracker for device=", dev_id );
	}else{
		log_errorln_dev_t( "Container access fail. Device=", dev_id );
		log_errorln_d("Error =", result);
	}

	return result;
}
