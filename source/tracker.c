#include "stdafx.h"
#include "tracker.h"
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
#include <linux/buffer_head.h>
#endif
#include "blk_util.h"

static container_sl_t Trackers;

int tracker_Init(void ){
	container_sl_init( &Trackers, sizeof( tracker_t ) );
	return SUCCESS;
}
int tracker_Done(void )
{
	int result = SUCCESS;

	result = tracker_RemoveAll();
	if (SUCCESS == result){
		if (SUCCESS != container_sl_done( &Trackers )){
			log_errorln_s( "Failed to free container =", "vsnap_Trackers" );
		}
	}
	else
		log_errorln("Cannot remove all tracking block device from tracking.");

	return result;
}


int tracker_FindByQueueAndSector( tracker_queue_t* pQueue, sector_t sector, tracker_t** ppTracker )
{
	int result = -ENODATA;

	content_sl_t* pContent = NULL;
	tracker_t* pTracker = NULL;
	CONTAINER_SL_FOREACH_BEGIN( Trackers, pContent )
	{
		pTracker = (tracker_t*)pContent;
		if ((pQueue == pTracker->pTrackerQueue) &&
			(sector >= blk_dev_get_start_sect( pTracker->pTargetDev )) &&
			(sector < (blk_dev_get_start_sect( pTracker->pTargetDev ) + blk_dev_get_capacity( pTracker->pTargetDev )))
		){
			*ppTracker = pTracker;
			result = SUCCESS;
			break;
		}
	}
	CONTAINER_SL_FOREACH_END( Trackers );

	return result;
}



int tracker_FindByDevId( dev_t dev_id, tracker_t** ppTracker )
{
	int result = -ENODATA;

	content_sl_t* pContent = NULL;
	tracker_t* pTracker = NULL;
	CONTAINER_SL_FOREACH_BEGIN( Trackers, pContent )
	{
		pTracker = (tracker_t*)pContent;
		if (pTracker->original_dev_id == dev_id){
			*ppTracker = pTracker;
			result =  SUCCESS;	//found!
			break;
		}
	}
	CONTAINER_SL_FOREACH_END( Trackers );

	return result;
}


int tracker_EnumDevId( int max_count, dev_t* p_dev_id, int* p_count )
{
	int result = -ENODATA;
	int count = 0;
	content_sl_t* pContent = NULL;
	tracker_t* pTracker = NULL;
	CONTAINER_SL_FOREACH_BEGIN( Trackers, pContent )
	{
		pTracker = (tracker_t*)pContent;
		if (count < max_count){
			p_dev_id[count] = pTracker->original_dev_id;
			++count;
			result = SUCCESS;
		}
		else{
			result = -ENOBUFS;
			break;
		}
	}
	CONTAINER_SL_FOREACH_END( Trackers );
	*p_count = count;

	{
		int inx = 0;
		for (; inx < count; ++inx){
			log_traceln_dev_t( "\tdev_id=", p_dev_id[inx] );
		}
	}
	//
	return result;
}


int tracker_EnumCbtInfo( int max_count, struct cbt_info_s* p_cbt_info, int* p_count )
{
	int result = -ENODATA;
	int count = 0;
	content_sl_t* pContent = NULL;
	tracker_t* pTracker = NULL;
	CONTAINER_SL_FOREACH_BEGIN( Trackers, pContent )
	{
		pTracker = (tracker_t*)pContent;
		if (count < max_count){
			if (pTracker->underChangeTracking){

				p_cbt_info[count].dev_id.major = MAJOR( pTracker->original_dev_id );
				p_cbt_info[count].dev_id.minor = MINOR( pTracker->original_dev_id );

				if (pTracker->cbt_map){
					p_cbt_info[count].cbt_map_size = pTracker->cbt_map->map_size;
					p_cbt_info[count].snap_number = (unsigned char)pTracker->cbt_map->snap_number_previous;
					veeam_uuid_copy( (veeam_uuid_t*)(p_cbt_info[count].generationId), &pTracker->cbt_map->generationId );
				}
				else{
					p_cbt_info[count].cbt_map_size = 0;
					p_cbt_info[count].snap_number = 0;
				}

				p_cbt_info[count].dev_capacity = sector_to_streamsize( blk_dev_get_capacity( pTracker->pTargetDev ) );

				++count;
				result = SUCCESS;
			}
		}
		else{
			result = -ENOBUFS;
			break;	//don`t continue
		}
	}
	CONTAINER_SL_FOREACH_END( Trackers );
	*p_count = count;
	return result;
}

void _tracker_cbt_initialize( tracker_t* pTracker, unsigned long long snapshot_id )
{
	log_traceln( "Do not create CBT map" );

	pTracker->underChangeTracking = false;
	pTracker->snapshot_id = snapshot_id;

	pTracker->cbt_map = NULL;
}

void tracker_cbt_start( tracker_t* pTracker, unsigned long long snapshot_id, unsigned int cbt_block_size_degree, sector_t device_capacity )
{
	log_traceln( "." );
	{
		cbt_map_t* cbt_map = cbt_map_create( (cbt_block_size_degree - SECTOR512_SHIFT), device_capacity );
		pTracker->cbt_map = cbt_map_get_resource( cbt_map );
	}
	pTracker->cbt_block_size_degree = cbt_block_size_degree;
	pTracker->snapshot_id = snapshot_id;
	pTracker->device_capacity = device_capacity;
	pTracker->underChangeTracking = true;

}

int tracker_Create( unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree, tracker_t** ppTracker )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	*ppTracker = NULL;

	pTracker = (tracker_t*)container_sl_new( &Trackers );
	if (NULL==pTracker)
		return -ENOMEM;

	atomic_set( &pTracker->Freezed, false);

	pTracker->original_dev_id = dev_id;

	result = blk_dev_open( pTracker->original_dev_id, &pTracker->pTargetDev );
	if (result != SUCCESS)
		return result;

	do{
		struct super_block* pSb = NULL;

		log_traceln_dev_t( "dev_id ", pTracker->original_dev_id );
		log_traceln_llx( "SectorStart    =", (unsigned long long)blk_dev_get_start_sect( pTracker->pTargetDev ) );
		log_traceln_llx( "SectorsCapacity=", (unsigned long long)blk_dev_get_capacity( pTracker->pTargetDev ) );


		if (snapshot_id == 0)
			tracker_cbt_start( pTracker, snapshot_id, cbt_block_size_degree, blk_dev_get_capacity( pTracker->pTargetDev ) );
		else
			_tracker_cbt_initialize( pTracker, snapshot_id );

		if (pTracker->pTargetDev->bd_super != NULL){
			pSb = freeze_bdev( pTracker->pTargetDev );
			if (NULL == pSb){
				log_errorln_dev_t( "freeze_bdev failed for device=.", pTracker->original_dev_id );
				result = -ENODEV;
				break;
			}
			log_traceln( "freezed");
		}
		else{
			log_warnln( "Device havn`t super block. It`s cannot be freeze.");
		}

		result = tracker_queue_Ref( bdev_get_queue( pTracker->pTargetDev ), &pTracker->pTrackerQueue );

		if (pSb != NULL){
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
			thaw_bdev( pTracker->pTargetDev, pSb );
#else
			if (SUCCESS < thaw_bdev( pTracker->pTargetDev, pSb )){
				log_errorln_dev_t( "thaw_bdev failed for device=", pTracker->original_dev_id );
			}
#endif
			log_traceln( "thawed." );

			pSb = NULL;
		}
	}while(false);

	if (SUCCESS ==result){
		*ppTracker = pTracker;
	}else{
		int remove_status = SUCCESS;

		log_errorln_dev_t( "Failed for device=", pTracker->original_dev_id );

		remove_status = tracker_Remove(pTracker);
		if ((SUCCESS == remove_status) || (-ENODEV == remove_status)){
			pTracker = NULL;
		}
		else{
			log_errorln_d( "Failed to remove tracker. result=", (0 - remove_status) );
		}
	}

	return result;
}

int _tracker_Remove( tracker_t* pTracker )
{
	int result = SUCCESS;

	log_traceln(".");

	if (NULL != pTracker->pTargetDev){
		struct super_block* pSb = NULL;

		if (pTracker->pTargetDev->bd_super != NULL){
			log_traceln( "freezing" );
			pSb = freeze_bdev( pTracker->pTargetDev );
			if (NULL != pSb){
				log_traceln( "freezed" );
			}
		}
		else{
			log_warnln( "Device havn`t super block. It`s cannot be freeze." );
		}

		if (NULL != pTracker->pTrackerQueue){
			tracker_queue_Unref( pTracker->pTrackerQueue );
			pTracker->pTrackerQueue = NULL;
		}

		if (pSb != NULL){
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
			thaw_bdev( pTracker->pTargetDev, pSb );
			log_traceln( "thawed" );
#else
			if (SUCCESS < thaw_bdev( pTracker->pTargetDev, pSb )){
				log_errorln_dev_t( "thaw_bdev failed for device=.", pTracker->original_dev_id );
			}
			else{
				log_traceln( "thawed");
			}
#endif
			pSb = NULL;
		}

		blk_dev_close( pTracker->pTargetDev );

		pTracker->pTargetDev = NULL;
		log_traceln_dev_t( "Unref device dev_id=", pTracker->original_dev_id );

	}else{
		result=-ENODEV;
	}

	if (NULL != pTracker->cbt_map){
		cbt_map_put_resource( pTracker->cbt_map );
		pTracker->cbt_map = NULL;
	}

	return result;
}


int tracker_Remove(tracker_t* pTracker)
{
	int result = _tracker_Remove( pTracker );

	container_sl_free( &pTracker->content );

	return result;
}


int tracker_RemoveAll(void )
{
	int result = SUCCESS;
	int status;
	content_sl_t* pCnt = NULL;

	log_traceln("Removing all devices from CBT");

	while (NULL != (pCnt = container_sl_get_first( &Trackers ))){
		tracker_t* pTracker = (tracker_t*)pCnt;

		status = _tracker_Remove( pTracker );
		if (status != SUCCESS){
			log_errorln_dev_t( "Cannot remove device from CBT. device=", pTracker->original_dev_id );
			log_errorln_d( "error=", 0 - status );
		}

		content_sl_free( pCnt );
		pCnt = NULL;
	}

	return result;
}

int tracker_CbtBitmapSet( tracker_t* pTracker, sector_t sector, sector_t sector_cnt )
{
	int res = SUCCESS;
	if (pTracker->device_capacity == blk_dev_get_capacity( pTracker->pTargetDev )){
		if (pTracker->cbt_map)
			res = cbt_map_set( pTracker->cbt_map, sector, sector_cnt );
	}
	else{
		log_warnln( "Device resize detected" );
		res = -EINVAL;
	}
	if (SUCCESS != res){ //cbt corrupt
		if (pTracker->cbt_map){
			log_warnln( "CBT fault" );
			pTracker->cbt_map->active = false;
		}
	}
	return res;
}

bool tracker_CbtBitmapLock( tracker_t* pTracker )
{
	bool result = false;
	if (pTracker->cbt_map){
		cbt_map_read_lock( pTracker->cbt_map );

		if (pTracker->cbt_map->active){
			result = true;
		}
		else
			cbt_map_read_unlock( pTracker->cbt_map );
	}
	return result;
}

void tracker_CbtBitmapUnlock( tracker_t* pTracker )
{
	if (pTracker->cbt_map)
		cbt_map_read_unlock( pTracker->cbt_map );
}

int _tracker_freeze( tracker_t* p_tracker )
{
	int result = SUCCESS;
	defer_io_t* defer_io = NULL;
	struct super_block* pSb = NULL;

	log_traceln_dev_t( "dev_id=", p_tracker->original_dev_id );

	result = blk_freeze_bdev( p_tracker->original_dev_id, p_tracker->pTargetDev, &pSb );
	if (result != SUCCESS){
		return result;
	}

	result = defer_io_create( p_tracker->original_dev_id, p_tracker->pTargetDev, &defer_io );
	if (result != SUCCESS){
		log_errorln( "defer_io_create failed." );
	}else{
		p_tracker->defer_io = defer_io_get_resource( defer_io );

		atomic_set( &p_tracker->Freezed, true );

		if (p_tracker->cbt_map != NULL){

			cbt_map_write_lock( p_tracker->cbt_map );

			cbt_map_switch( p_tracker->cbt_map );

			cbt_map_write_unlock( p_tracker->cbt_map );

			log_traceln_ld( "Snapshot created. New snap number=", p_tracker->cbt_map->snap_number_active );
		}
	}
	if (pSb != NULL){
		blk_thaw_bdev( p_tracker->original_dev_id, p_tracker->pTargetDev, pSb );
		pSb = NULL;
	}
	return result;

}


int tracker_capture_snapshot( snapshot_t* snapshot )
{
	int result = SUCCESS;
	int inx = 0;

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
		tracker_t* tracker = NULL;
		result = tracker_FindByDevId( snapshot->dev_id_set[inx], &tracker );
		if (result != SUCCESS){
			log_errorln( "Cannot find device." );
			break;
		}

		result = _tracker_freeze( tracker );
		if (result != SUCCESS){
			log_errorln("failed." );
			break;
		}
	}
	if (result != SUCCESS)
		return result;

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
		tracker_t* p_tracker = NULL;
		result = tracker_FindByDevId( snapshot->dev_id_set[inx], &p_tracker );

#ifdef SNAPSTORE
		if (snapstore_device_is_corrupted( p_tracker->defer_io->snapstore_device )){
#else
		if (snapshotdata_IsCorrupted( p_tracker->defer_io->snapshotdata, snapshot->dev_id_set[inx] )){
#endif
			log_errorln_dev_t( "Failed to freeze devices. Snapshot data already corrupt for ", snapshot->dev_id_set[inx] );
			result = -EDEADLK;
			break;
		}
	}

	if (result != SUCCESS){
		int status = tracker_release_snapshot( snapshot );
		if (status != SUCCESS)
			log_errorln_llx( "Cannot release snapshot=", snapshot->id );
	}
	return result;
}


int _tracker_release_snapshot( tracker_t* pTracker )
{
	int result = SUCCESS;
	struct super_block* pSb = NULL;
	defer_io_t* defer_io = pTracker->defer_io;

	if (defer_io != NULL){
		down_write( &defer_io->flush_lock );

		log_traceln_dev_t( "dev_id=", pTracker->original_dev_id );

		if (pTracker->cbt_map != NULL){
			if (pTracker->cbt_map->active){
				log_traceln_ld( "Active snap number=", pTracker->cbt_map->snap_number_active );
			}
			else{
				log_traceln( "CBT is not active" );
			}
		}
		else{
			log_traceln( "CBT is not initialized" );
		}

		//clear freeze flag
		atomic_set( &pTracker->Freezed, false );

		defer_io_destroy( defer_io );

		up_write( &defer_io->flush_lock );

		if (pTracker->pTargetDev->bd_super != NULL){
			log_traceln_dev_t( "freezing device ", pTracker->original_dev_id );
			pSb = freeze_bdev( pTracker->pTargetDev );
			if (NULL == pSb){
				log_errorln_dev_t( "freeze_bdev failed for device=", pTracker->original_dev_id );
				return result = -ENODEV;
			}
			log_traceln( "freezed." );
		}
		else{
			log_traceln( "Device havn`t super block. It`s cannot be freeze." );
		}
		pTracker->defer_io = NULL;
		defer_io_put_resource( defer_io );
	}
	if (pSb != NULL){
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
		thaw_bdev( pTracker->pTargetDev, pSb );
		log_traceln( "thawed." );
#else
		if (SUCCESS < thaw_bdev( pTracker->pTargetDev, pSb )){
			log_errorln( "thawed." );
		}
		else{
			log_traceln_dev_t( "thaw_bdev for device=", pTracker->original_dev_id );
		}
#endif

		pSb = NULL;
	}
	return result;
}


int tracker_release_snapshot( snapshot_t* snapshot )
{
	int result = SUCCESS;
	int inx = 0;
	log_traceln( "." );

	for (; inx < snapshot->dev_id_set_size; ++inx){
		int status;
		tracker_t* p_tracker = NULL;
		dev_t dev = snapshot->dev_id_set[inx];

		status = tracker_FindByDevId( dev, &p_tracker );
		if (status == SUCCESS){
			status = _tracker_release_snapshot( p_tracker );
			if (status != SUCCESS){
				log_errorln_dev_t( "Cannot release snapshot for device=", dev );
				result = status;
				break;
			}
		}
		else{
			log_errorln_dev_t( "Cannot find device for release snapshot for device=", dev );
		}
	}

	return result;
}


void tracker_print_state( void )
{
	size_t sz;
	tracker_t** trackers;
	int tracksers_cnt = 0;

	tracksers_cnt = container_sl_length( &Trackers );
	sz = tracksers_cnt * sizeof( tracker_t* );
	trackers = dbg_kzalloc( sz, GFP_KERNEL );
	if (trackers == NULL){
		log_errorln_sz( "Cannot allocate memory. size=", sz );
		return;
	}

	do{
		size_t inx = 0;
		content_sl_t* pContent = NULL;
		CONTAINER_SL_FOREACH_BEGIN( Trackers, pContent )
		{
			tracker_t* pTracker = (tracker_t*)pContent;

			trackers[inx] = pTracker;
			inx++;
			if (inx >= tracksers_cnt)
				break;
		}
		CONTAINER_SL_FOREACH_END( Trackers );

		for (inx = 0; inx < tracksers_cnt; ++inx){
			if (NULL != trackers[inx]){
				pr_warn( "\n" );
				pr_warn( "%s:\n", __FUNCTION__ );

				pr_warn( "tracking device =%d.%d\n", MAJOR( trackers[inx]->original_dev_id ), MINOR( trackers[inx]->original_dev_id ) );
				if (trackers[inx]->defer_io)
					defer_io_print_state( trackers[inx]->defer_io );
			}
		}
	} while (false);
	dbg_kfree( trackers );
}
