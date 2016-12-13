#include "stdafx.h"
#include "container.h"
#include "container_spinlocking.h"
#include "queue_spinlocking.h"
#include "tracker_queue.h"
#include "range.h"
#include "rangeset.h"
#include "rangelist.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "blk_dev_utile.h"
#include "shared_resource.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"
#include "defer_io.h"
#include "snapshot.h"
#include "cbt_map.h"
#include "veeamsnap_ioctl.h"
#include "tracker.h"


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
#include <linux/buffer_head.h>
#endif


//////////////////////////////////////////////////////////////////////////
static container_sl_t Trackers;
//////////////////////////////////////////////////////////////////////////

int tracker_Init(void ){
	return container_sl_init( &Trackers, sizeof(tracker_t), NULL );
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
//////////////////////////////////////////////////////////////////////////
#if 1
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
#else
typedef struct _FindByQueue_s
{
	tracker_queue_t* pQueue;
	sector_t sector;
	content_sl_t* pContent;
}FindByQueue_t;

int _FindByQueue_cb(content_sl_t* pContent, void* parameter)
{
	FindByQueue_t* pParam = (FindByQueue_t*)parameter;
	tracker_t* pTracker = (tracker_t*)pContent;

	if ((pParam->pQueue == pTracker->pTrackerQueue) &&
		(pParam->sector >= blk_dev_get_start_sect( pTracker->pTargetDev )) &&
		(pParam->sector < (blk_dev_get_start_sect( pTracker->pTargetDev ) + blk_dev_get_capacity( pTracker->pTargetDev )))
	){
		pParam->pContent = pContent;
		return SUCCESS;	//don`t continue
	}
	return ENODATA; //continue
}
/*
 * return:
 *     SUCCESS if found;
 *     -ENODATA if not found
 *     anything else in error case.
 *
 */
int tracker_FindByQueueAndSector(tracker_queue_t* pQueue, sector_t sector, tracker_t** ppTracker)
{
	int result = SUCCESS;
	FindByQueue_t param = {
		.pQueue = pQueue,
		.sector = sector,
		.pContent = NULL
	};

	result = container_sl_enum( &Trackers, _FindByQueue_cb, &param);

	if ( SUCCESS==result ){
		*ppTracker = (tracker_t*) param.pContent;
		if ( (NULL==param.pContent) )
			result = -ENODATA;
	}else
		*ppTracker = NULL;
	return result;
}
#endif
//////////////////////////////////////////////////////////////////////////
#if 1
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
#else
typedef struct _FindByDevId_s
{
	dev_t dev_id;
	content_sl_t* pContent;
}FindByDevId_t;

int _FindByDevId_cb(content_sl_t* pContent, void* parameter)
{
	FindByDevId_t* pParam = (FindByDevId_t*)parameter;
	tracker_t* pTracker = (tracker_t*)pContent;

	if (pTracker->original_dev_id == pParam->dev_id){
		pParam->pContent = pContent;
		return SUCCESS;	//don`t continue
	}
	return ENODATA; //continue
}
/*
 * return:
 *     SUCCESS if found;
 *     -ENODATA if not found
 *     anything else in error case.
 */
int tracker_FindByDevId(dev_t dev_id, tracker_t** ppTracker)
{
	int result = SUCCESS;
	FindByDevId_t param = {
		.dev_id = dev_id,
		.pContent = NULL
	};

	result = container_sl_enum( &Trackers, _FindByDevId_cb, &param);

	if ( SUCCESS==result ){
		*ppTracker = (tracker_t*) param.pContent;
		if ( (NULL==param.pContent) )
			result = -ENODATA;
	}else
		*ppTracker = NULL;
	return result;
}
#endif
//////////////////////////////////////////////////////////////////////////
#if 1
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
#else
typedef struct _EnumDevId_s
{
	int max_count;
	dev_t* p_dev_id;
	int count;
	int status;
}EnumDevId_t;

int _EnumDevId_cb(content_sl_t* pContent, void* parameter)
{
	EnumDevId_t* pParam = (EnumDevId_t*)parameter;
	tracker_t* pTracker = (tracker_t*)pContent;

	if ( pParam->count < pParam->max_count){
		log_traceln_dev_t( "\tdev_id=", pTracker->original_dev_id );

		pParam->p_dev_id[pParam->count] = pTracker->original_dev_id;
		++pParam->count;
	}else{
		pParam->status = -ENOBUFS;
		return SUCCESS;	//don`t continue
	}
	return ENODATA; //continue
}

int tracker_EnumDevId( int max_count, dev_t* p_dev_id, int* p_count )
{
	int result = SUCCESS;
	EnumDevId_t param = {
		.max_count = max_count,
		.p_dev_id = p_dev_id,
		.count = 0,
		.status = SUCCESS
	};

	log_traceln("exec");

	result = container_sl_enum( &Trackers, _EnumDevId_cb, &param);
	{
		int i=0;
		for (;i<param.count; ++i){
			log_traceln_dev_t( "\tdev_id=", p_dev_id[i] );
		}
	}

	if (SUCCESS==result){
		*p_count = param.count;
		result = param.status;
	}

	return result;
}
#endif
//////////////////////////////////////////////////////////////////////////
#if 1
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
					memcpy( &(p_cbt_info[count].generationId), &(pTracker->cbt_map->generationId), 16 );
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
#else
typedef struct _EnumCbtInfo_s
{
	int max_count;
	struct cbt_info_s* p_cbt_info;
	int count;
	int status;
}EnumCbtInfo_t;

int _EnumCbtInfo_cb( content_sl_t* pContent, void* parameter )
{
	EnumCbtInfo_t* pParam = (EnumCbtInfo_t*)parameter;
	tracker_t* pTracker = (tracker_t*)pContent;

	if (pParam->count < pParam->max_count){
		if (pTracker->underChangeTracking){

			pParam->p_cbt_info[pParam->count].dev_id.major = MAJOR( pTracker->original_dev_id );
			pParam->p_cbt_info[pParam->count].dev_id.minor = MINOR( pTracker->original_dev_id );

			if (pTracker->cbt_map){
				pParam->p_cbt_info[pParam->count].cbt_map_size = pTracker->cbt_map->map_size;
				pParam->p_cbt_info[pParam->count].snap_number = (unsigned char)pTracker->cbt_map->snap_number_previous;
                memcpy( &(pParam->p_cbt_info[pParam->count].generationId), &(pTracker->cbt_map->generationId), 16 );
			}else{
				pParam->p_cbt_info[pParam->count].cbt_map_size = 0;
				pParam->p_cbt_info[pParam->count].snap_number = 0;
			}

			pParam->p_cbt_info[pParam->count].dev_capacity = sector_to_streamsize( blk_dev_get_capacity( pTracker->pTargetDev ) );

			++pParam->count;
		}

	}
	else{
		pParam->status = -ENOBUFS;
		return SUCCESS;	//don`t continue
	}
	return ENODATA; //continue
}

int tracker_EnumCbtInfo( int max_count, struct cbt_info_s* p_cbt_info, int* p_count )
{
	int result = SUCCESS;
	EnumCbtInfo_t param = {
		.max_count = max_count,
		.p_cbt_info = p_cbt_info,
		.count = 0,
		.status = SUCCESS
	};

	result = container_sl_enum( &Trackers, _EnumCbtInfo_cb, &param );
	if ((SUCCESS==result) && (SUCCESS==param.status)){
		int i = 0;
		log_traceln_d( "found devices count=", param.count );
		for (; i < param.count; ++i){
			log_traceln_dev_id_s( "dev_id:", p_cbt_info[i].dev_id );
		}
		result = param.status;
		*p_count = param.count;
	}

	return result;
}
#endif
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
int tracker_Create( unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree, tracker_t** ppTracker )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	log_traceln(".");
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
		log_traceln_p( "bd_super=", pTracker->pTargetDev->bd_super );
		log_traceln_llx( "SectorStart    =", (unsigned long long)blk_dev_get_start_sect( pTracker->pTargetDev ) );
		log_traceln_llx( "SectorsCapacity=", (unsigned long long)blk_dev_get_capacity( pTracker->pTargetDev ) );


		if (snapshot_id == 0){
			cbt_map_t* cbt_map = cbt_map_create( (cbt_block_size_degree - SECTOR512_SHIFT), blk_dev_get_capacity( pTracker->pTargetDev ) );

			pTracker->underChangeTracking = true;
			pTracker->snapshot_id = 0;

			pTracker->cbt_map = cbt_map_get_resource( cbt_map );
		}
		else{
			pTracker->underChangeTracking = false;
			pTracker->snapshot_id = snapshot_id;

			pTracker->cbt_map = NULL;
		}

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
			log_traceln( "Device havn`t super block. It`s cannot be freeze.");
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

int __tracker_Remove( tracker_t* pTracker )
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
			log_traceln( "Device havn`t super block. It`s cannot be freeze." );
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

//////////////////////////////////////////////////////////////////////////
int tracker_Remove(tracker_t* pTracker)
{
	int result;

	log_traceln( "." );

	result = __tracker_Remove( pTracker );

	container_sl_free( &pTracker->content );

	return result;
}
//////////////////////////////////////////////////////////////////////////

int tracker_RemoveAll(void )
{
	int result = SUCCESS;
	int status;
	content_sl_t* pCnt = NULL;

	log_traceln("Removing all devices from CBT");

	while (NULL != (pCnt = container_sl_get_first( &Trackers ))){
		tracker_t* pTracker = (tracker_t*)pCnt;

		status = __tracker_Remove( pTracker );
		if (status != SUCCESS){
			log_errorln_dev_t( "Cannot remove device from CBT. device=", pTracker->original_dev_id );
			log_errorln_d( "error=", 0 - status );
		}

		content_sl_free( pCnt );
		pCnt = NULL;
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void tracker_CbtBitmapSet( tracker_t* pTracker, sector_t sector, sector_t sector_cnt )
{
	int res;
	res = cbt_map_set( pTracker->cbt_map, sector, sector_cnt );
	if (SUCCESS != res){
		log_errorln_dev_t( "Tracking failed for device ", pTracker->original_dev_id );
		log_errorln_sect( "sector num=", sector );
		log_errorln_sect( "sectors cnt=", sector_cnt );

		res = tracker_Remove( pTracker );
		if (SUCCESS != res){
			log_errorln_d( "Failed to remove tracker. code=", (0 - res) );
		}
	}
}
//////////////////////////////////////////////////////////////////////////
void tracker_CbtBitmapLock( tracker_t* pTracker )
{
	cbt_map_read_lock( pTracker->cbt_map );
}
//////////////////////////////////////////////////////////////////////////
void tracker_CbtBitmapUnlock( tracker_t* pTracker )
{
	cbt_map_read_unlock( pTracker->cbt_map );
}
//////////////////////////////////////////////////////////////////////////

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
//////////////////////////////////////////////////////////////////////////
int tracker_Freeze( snapshot_t* p_snapshot )
{
	int result = SUCCESS;
	int inx = 0;

	for (inx = 0; inx < p_snapshot->snapshot_map_length; ++inx){
		tracker_t* p_tracker = NULL;
		result = tracker_FindByDevId( p_snapshot->p_snapshot_map[inx].DevId, &p_tracker );
		if (result != SUCCESS){
			log_errorln( "Cannot find device." );
			break;
		}

		result = _tracker_freeze( p_tracker );
		if (result != SUCCESS){
			log_errorln("failed." );
			break;
		}
	}

	return result;
}
//////////////////////////////////////////////////////////////////////////

int _tracker_unfreeze( tracker_t* pTracker )
{
	int result = SUCCESS;
	struct super_block* pSb = NULL;
	defer_io_t* defer_io = pTracker->defer_io;

	if (defer_io != NULL){
		down_write( &defer_io->flush_lock );

		log_traceln_dev_t( "dev_id=", pTracker->original_dev_id );

		if (pTracker->cbt_map != NULL)
			log_traceln_ld( "Snapshot freed. Active snap number=", pTracker->cbt_map->snap_number_active );

		//clear freeze flag
		atomic_set( &pTracker->Freezed, false );

		defer_io_close( defer_io );

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
//////////////////////////////////////////////////////////////////////////
int tracker_Unfreeze( snapshot_t* p_snapshot )
{
	int result = SUCCESS;
	int inx = 0;
	log_traceln( "." );

	for (; inx < p_snapshot->snapshot_map_length; ++inx){
		int status;
		tracker_t* p_tracker = NULL;
		dev_t dev = p_snapshot->p_snapshot_map[inx].DevId;

		status = tracker_FindByDevId( dev, &p_tracker );
		if (status == SUCCESS){
			status = _tracker_unfreeze( p_tracker );
			if (status != SUCCESS){
				log_errorln_dev_t( "Cannot unfreeze device=", dev );
				result = status;
				break;
			}
		}
		else{
			log_errorln_dev_t( "Cannot find device for unfreezing. device=", dev );
		}
	}

	return result;
}
//////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////