#include "stdafx.h"
#include "container.h"
#include "container_spinlocking.h"
#include "snapshot.h"
#include "range.h"
#include "rangeset.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"
#include "shared_resource.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"
#include "defer_io.h"
#include "tracker_queue.h"
#include "veeamsnap_ioctl.h"
#include "snapimage.h"
#include "cbt_map.h"
#include "tracker.h"
//////////////////////////////////////////////////////////////////////////
int _snapshot_destroy( snapshot_t* p_snapshot );

//////////////////////////////////////////////////////////////////////////
static container_t Snapshots;

//////////////////////////////////////////////////////////////////////////
int snapshot_Init( void )
{
	return container_init( &Snapshots, sizeof( snapshot_t ), NULL/*"vsnap_Snapshots"*/ );
}
//////////////////////////////////////////////////////////////////////////
int snapshot_Done( void )
{
	int result = SUCCESS;
	content_t* content;

	log_traceln( "Removing all snapshots" );

	while (NULL != (content = container_get_first( &Snapshots ))){
		int status = SUCCESS;
		snapshot_t* p_snapshot = (snapshot_t*)content;

		status = _snapshot_destroy( p_snapshot );
		if (status != SUCCESS){
			log_errorln_llx( "Failed to destroy snapshot. id=", p_snapshot->id );
			result = status;
		}
	}

	if (result == SUCCESS)
		result = container_done( &Snapshots );
	return result;
}
//////////////////////////////////////////////////////////////////////////
int _snapshot_New( dev_t* p_dev, int count, snapshot_t** pp_snapshot )
{
	int result = SUCCESS;
	snapshot_t* p_snapshot = NULL;
	snapshot_map_t* pSnapMap = NULL;

	do{
		int inx = 0;

		p_snapshot = (snapshot_t*)content_new( &Snapshots );
		if (NULL == p_snapshot){
			log_errorln( "Cannot allocate memory for snapshot structure." );
			result = -ENOMEM;
			break;
		}

		p_snapshot->id = (unsigned long long)( 0 ) + (unsigned long)(p_snapshot );

		p_snapshot->p_snapshot_map = NULL;
		p_snapshot->snapshot_map_length = 0;

		pSnapMap = (snapshot_map_t*)dbg_kzalloc( sizeof( snapshot_map_t ) * (1 + count), GFP_KERNEL );
		if (NULL == pSnapMap){
			log_errorln( "Cannot allocate memory for snapshot map." );
			result = -ENOMEM;
			break;
		}

		for (; inx < count; ++inx)
			pSnapMap[inx].DevId = p_dev[inx];


		p_snapshot->snapshot_map_length = count;
		p_snapshot->p_snapshot_map = pSnapMap;

		*pp_snapshot = p_snapshot;
		container_push_back( &Snapshots, &p_snapshot->content );
	} while (false);

	if (result != SUCCESS){
		if (pSnapMap != NULL){
			dbg_kfree( pSnapMap );
			pSnapMap = NULL;
		}
		if (p_snapshot != NULL){
			dbg_kfree( p_snapshot );
			p_snapshot = NULL;
		}
		content_free( &p_snapshot->content );
	}
	return result;
}
//////////////////////////////////////////////////////////////////////////
int _snapshot_Free( snapshot_t* p_snapshot )
{
	int result = SUCCESS;

	if (p_snapshot->p_snapshot_map != NULL){
		dbg_kfree( p_snapshot->p_snapshot_map );
		p_snapshot->p_snapshot_map = NULL;
		p_snapshot->snapshot_map_length = 0;
	}
	return result;
}
//////////////////////////////////////////////////////////////////////////
int _snapshot_Delete( snapshot_t* p_snapshot )
{
	int result;
	result = _snapshot_Free( p_snapshot );

	if (result == SUCCESS)
		content_free( &p_snapshot->content );
	return result;
}
//////////////////////////////////////////////////////////////////////////
typedef struct FindBySnapshotId_s
{
	unsigned long long id;
	content_t* pContent;
}FindBySnapshotId_t;

int _FindById_cb( content_t* pContent, void* parameter )
{
	FindBySnapshotId_t* pParam = (FindBySnapshotId_t*)parameter;
	snapshot_t* p_snapshot = (snapshot_t*)pContent;

	if (p_snapshot->id == pParam->id){
		pParam->pContent = pContent;
		return SUCCESS;	//don`t continue
	}
	return ENODATA; //continue
}
/*
* return:
*     SUCCESS if found;
*     ENODATA if not found
*     anything else in error case.
*/
int snapshot_FindById( unsigned long long id, snapshot_t** pp_snapshot )
{
	int result = SUCCESS;
	FindBySnapshotId_t param = {
		.id = id,
		.pContent = NULL
	};

	result = container_enum( &Snapshots, _FindById_cb, &param );

	if (SUCCESS == result){
		*pp_snapshot = (snapshot_t*)param.pContent;
		if ((NULL == param.pContent))
			result = -ENODATA;
	}
	else
		*pp_snapshot = NULL;
	return result;
}

int _snapshot_add_data( dev_t DevId )
{
	int result = SUCCESS;

	result = snapshotdata_FindByDevId( DevId, NULL );
	if (SUCCESS == result){
		log_traceln_dev_t( "Snapshot data exist for device=", DevId );
	}
	else if (-ENODATA == result){
		log_traceln_dev_t( "Failed to link device to snapshot data common disk. device=", DevId );

		result = snapshotdata_AddMemInfo( DevId, SNAPSHOTDATA_MEMORY_SIZE );
		if (result == SUCCESS){
			log_traceln_dev_t( "Snapshot data created in memory for device=", DevId );
		}
		else{
			log_traceln_dev_t( "Failed to create snapshot data in memory for device=", DevId );
		}
	}
	else{
		log_traceln_dev_t( "Failed to find snapshot data for device=", DevId );
	}

	return result;
}
//////////////////////////////////////////////////////////////////////////
int _snapshot_add_tracker( snapshot_map_t* p_dev_map, unsigned int cbt_block_size_degree, unsigned long long snapshot_id )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	log_traceln_dev_t( "Adding. dev_id=", p_dev_map->DevId );

	result = tracker_FindByDevId( p_dev_map->DevId, &pTracker );
	if (SUCCESS == result){
		if (pTracker->underChangeTracking)
			log_traceln_dev_t( "Device already under change tracking. Device=", p_dev_map->DevId );

		if (pTracker->snapshot_id != 0){
			log_errorln( "Device already in snapshot." );
			log_errorln_dev_t( "    Device=", p_dev_map->DevId );
			log_errorln_llx( "    snapshot_id=", pTracker->snapshot_id );
			result = -EBUSY;
		}

		pTracker->snapshot_id = snapshot_id;
	}
	else if (-ENODATA == result){
		result = tracker_Create( snapshot_id, p_dev_map->DevId, cbt_block_size_degree, &pTracker );
		if (SUCCESS != result)
			log_errorln_d( "Failed to create tracker. error=", (0 - result) );
	}
	else{
		log_errorln_dev_t( "Container access fail. Device=", p_dev_map->DevId );
		log_errorln_d( "Error =", (0 - result) );
	}

	return result;
}
//////////////////////////////////////////////////////////////////////////
int _snapshot_remove_device( dev_t dev_id )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	log_traceln_dev_t( "Removing. dev_id=", dev_id );

	result = tracker_FindByDevId( dev_id, &pTracker );
	if (result != SUCCESS){
		if (result == -ENODEV){
			log_errorln_dev_t( "Cannot find device by device id=", dev_id );
		}
		else{
			log_errorln_dev_t( "Failed to find device by device id=", dev_id );
		}
		return SUCCESS;
	}

	if (result != SUCCESS)
		return result;

	pTracker->snapshot_id = 0;

	do{
		if (!pTracker->underChangeTracking){
			result = tracker_Remove( pTracker );
			if (result != SUCCESS)
				break;
		}
		result = snapshotdata_CleanInfo( dev_id );
	} while (false);

	return result;
}
//////////////////////////////////////////////////////////////////////////
int _snapshot_cleanup( snapshot_t* p_snapshot )
{
	int result = SUCCESS;
	int inx = 0;
	unsigned long long snapshot_id = p_snapshot->id;

	for (; inx < p_snapshot->snapshot_map_length; ++inx){
		result = _snapshot_remove_device( p_snapshot->p_snapshot_map[inx].DevId );
		if (result != SUCCESS){
			log_errorln_dev_t( "Failed to remove device from snapshot. DevId=", p_snapshot->p_snapshot_map[inx].DevId );
		}
	}

	result = _snapshot_Delete( p_snapshot );
	if (result != SUCCESS){
		log_errorln_llx( "Failed to delete snapshot. snapshot_id=", snapshot_id );
	}
	return result;
}
//////////////////////////////////////////////////////////////////////////
int snapshot_Create( dev_t* p_dev, int count, unsigned int cbt_block_size_degree, unsigned long long* p_snapshot_id )
{
	snapshot_t* p_snapshot = NULL;
	int result = SUCCESS;
	int inx = 0;

	for (inx = 0; inx < count; ++inx){
		log_traceln_dev_t( "device=", p_dev[inx] );
	}

	result = _snapshot_New( p_dev, count, &p_snapshot );
	if (result != SUCCESS){
		log_errorln( "Cannot create snapshot object." );
		dbg_mem_track_off( );
		return result;
	}

	for (inx = 0; inx < count; ++inx){

		result = _snapshot_add_tracker( p_snapshot->p_snapshot_map + inx, cbt_block_size_degree, p_snapshot->id );
		if (result == SUCCESS){
			//snapshot data managing
			_snapshot_add_data( p_snapshot->p_snapshot_map[inx].DevId );
		}else if (result == -EALREADY){
			log_traceln_d( "Already under tracking device=", p_dev[inx] );
			result = SUCCESS;
		}
		else if (result != SUCCESS){
			log_errorln_dev_t( "Cannot add device to snapshot tracking. dev_id=", p_dev[inx] );
			break;
		}
	}

	if (result == SUCCESS)
		result = tracker_Freeze( p_snapshot );

	if (SUCCESS == result){
		*p_snapshot_id = p_snapshot->id;
		log_traceln_llx( "snapshot_id=", p_snapshot->id );

		if (result == SUCCESS){
			result = snapimage_create_for( p_dev, count );
			if (result != SUCCESS){
				log_errorln( "Cannot create snapshot image devices." );
			}
		}
	}
	else{
		int status = tracker_Unfreeze( p_snapshot );
		if (status != SUCCESS){
			log_errorln_llx( "Cannot unfreeze snapshot=", p_snapshot->id );
		}else{
			container_get( &p_snapshot->content );
			status = _snapshot_cleanup( p_snapshot );
			if (status != SUCCESS){
				log_errorln_llx( "Cannot destroy snapshot=", p_snapshot->id );
				container_push_back( &Snapshots, &p_snapshot->content );
			}
		}
	}

	return result;
}
//////////////////////////////////////////////////////////////////////////

int _snapshot_destroy( snapshot_t* p_snapshot )
{
	int result = SUCCESS;
	size_t inx;

	log_traceln_llx( "snapshot_id=", p_snapshot->id );
	//
	for (inx = 0; inx < p_snapshot->snapshot_map_length; ++inx){
		result = snapimage_stop( p_snapshot->p_snapshot_map[inx].DevId );
		if (result != SUCCESS){
			log_errorln_dev_t( "Failed to remove device snapshot image. DevId=", p_snapshot->p_snapshot_map[inx].DevId );
		}
	}

	result = tracker_Unfreeze( p_snapshot );
	if (result != SUCCESS){
		log_errorln_llx( "Failed to release snapshot. snapshot_id=", p_snapshot->id );
		return result;
	}

	for (inx = 0; inx < p_snapshot->snapshot_map_length; ++inx){
		result = snapimage_destroy( p_snapshot->p_snapshot_map[inx].DevId );
		if (result != SUCCESS){
			log_errorln_dev_t( "Failed to remove device snapshot image. DevId=", p_snapshot->p_snapshot_map[inx].DevId );
		}
	}

	return _snapshot_cleanup( p_snapshot );
}

int snapshot_Destroy( unsigned long long snapshot_id )
{
	int result = SUCCESS;
	snapshot_t* p_snapshot = NULL;

	result = snapshot_FindById( snapshot_id, &p_snapshot );
	if (result != SUCCESS){
		log_errorln_llx( "Cannot find snapshot by snapshot_id=", snapshot_id );
		return result;
	}
	container_get( &p_snapshot->content );
	result = _snapshot_destroy( p_snapshot );
	if (result != SUCCESS){
		container_push_back( &Snapshots, &p_snapshot->content );
	}
	return result;
}

//////////////////////////////////////////////////////////////////////////
