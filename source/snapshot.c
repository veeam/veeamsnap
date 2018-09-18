#include "stdafx.h"
#include "snapshot.h"
#include "tracker.h"
#include "snapimage.h"


static container_t Snapshots;

int _snapshot_destroy( snapshot_t* p_snapshot );


int snapshot_Init( void )
{
	return container_init( &Snapshots, sizeof( snapshot_t ) );
}

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

	if (result == SUCCESS){
		if (SUCCESS != (result = container_done( &Snapshots ))){
			log_errorln( "Container is not empty" );
		};
	}
	return result;
}

int _snapshot_New( dev_t* p_dev, int count, snapshot_t** pp_snapshot )
{
	int result = SUCCESS;
	snapshot_t* p_snapshot = NULL;
	dev_t* snap_set = NULL;

	do{
		p_snapshot = (snapshot_t*)content_new( &Snapshots );
		if (NULL == p_snapshot){
			log_errorln( "Cannot allocate memory for snapshot structure." );
			result = -ENOMEM;
			break;
		}

		p_snapshot->id = (unsigned long long)( 0 ) + (unsigned long)(p_snapshot );

		p_snapshot->dev_id_set = NULL;
		p_snapshot->dev_id_set_size = 0;

		{
			size_t buffer_length = sizeof( dev_t ) * count;
			snap_set = (dev_t*)dbg_kzalloc( buffer_length, GFP_KERNEL );
			if (NULL == snap_set){
			log_errorln( "Cannot allocate memory for snapshot map." );
			result = -ENOMEM;
			break;
		}
			memcpy( snap_set, p_dev, buffer_length );
		}

		p_snapshot->dev_id_set_size = count;
		p_snapshot->dev_id_set = snap_set;

		*pp_snapshot = p_snapshot;
		container_push_back( &Snapshots, &p_snapshot->content );
	} while (false);

	if (result != SUCCESS){
		if (snap_set != NULL){
			dbg_kfree( snap_set );
			snap_set = NULL;
		}
		if (p_snapshot != NULL){
			dbg_kfree( p_snapshot );
			p_snapshot = NULL;
		}
		content_free( &p_snapshot->content );
	}
	return result;
}

int _snapshot_Free( snapshot_t* snapshot )
{
	int result = SUCCESS;

	if (snapshot->dev_id_set != NULL){
		dbg_kfree( snapshot->dev_id_set );
		snapshot->dev_id_set = NULL;
		snapshot->dev_id_set_size = 0;
	}
	return result;
}

int _snapshot_Delete( snapshot_t* p_snapshot )
{
	int result;
	result = _snapshot_Free( p_snapshot );

	if (result == SUCCESS)
		content_free( &p_snapshot->content );
	return result;
}

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

int _snapshot_check_data( dev_t dev_id )
{
#ifdef SNAPSTORE
	int result = SUCCESS;
#else
	int result = snapshotdata_FindByDevId( dev_id, NULL );
#endif //SNAPSTORE
	if (SUCCESS != result){
		log_errorln_dev_t( "Failed to find snapshot data for device=", dev_id );
		return result;
	}

	//log_traceln_dev_t( "Snapshot data exist for device=", dev_id );
	return SUCCESS;
		}

int _snapshot_add_tracker( dev_t dev_id, unsigned int cbt_block_size_degree, unsigned long long snapshot_id )
{
	int result = SUCCESS;
	tracker_t* pTracker = NULL;

	log_traceln_dev_t( "dev_id=", dev_id );

	result = tracker_FindByDevId( dev_id, &pTracker );
	if (SUCCESS == result){
		if (pTracker->underChangeTracking)
			log_traceln_dev_t( "Device already under change tracking. Device=", dev_id );

		if (pTracker->snapshot_id != 0){
			log_errorln( "Device already in snapshot." );
			log_errorln_dev_t( "    Device=", dev_id );
			log_errorln_llx( "    snapshot_id=", pTracker->snapshot_id );
			result = -EBUSY;
		}

		pTracker->snapshot_id = snapshot_id;
	}
	else if (-ENODATA == result){
		result = tracker_Create( snapshot_id, dev_id, cbt_block_size_degree, &pTracker );
		if (SUCCESS != result)
			log_errorln_d( "Failed to create tracker. error=", result );
	}
	else{
		log_errorln_dev_t( "Container access fail. Device=", dev_id );
		log_errorln_d( "Error =", result );
	}

	return result;
}

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
#ifdef SNAPSTORE
#else
	do{
		snapshotdata_t* snapshotdata = NULL;

		if (!pTracker->underChangeTracking){
			result = tracker_Remove( pTracker );
			if (result != SUCCESS)
				break;
		}

		result = snapshotdata_FindByDevId( dev_id, &snapshotdata );
		if (result != SUCCESS)
			break;

		result = snapshotdata_Destroy( snapshotdata );
	} while (false);
#endif //SNAPSTORE
	return result;
}

int _snapshot_cleanup( snapshot_t* snapshot )
{
	int result = SUCCESS;
	int inx = 0;
	unsigned long long snapshot_id = snapshot->id;

	//log_warnln_llx( "DEBUG! snapshot id=", snapshot_id );

	for (; inx < snapshot->dev_id_set_size; ++inx){
		result = _snapshot_remove_device( snapshot->dev_id_set[inx] );
		if (result != SUCCESS){
			log_errorln_dev_t( "Failed to remove device from snapshot. DevId=", snapshot->dev_id_set[inx] );
		}
	}

	result = _snapshot_Delete( snapshot );
	if (result != SUCCESS){
		log_errorln_llx( "Failed to delete snapshot. snapshot_id=", snapshot_id );
	}
	return result;
}

int snapshot_Create( dev_t* dev_id_set, unsigned int dev_id_set_size, unsigned int cbt_block_size_degree, unsigned long long* psnapshot_id )
{
	snapshot_t* snapshot = NULL;
	int result = SUCCESS;
	unsigned int inx = 0;

	//log_warnln_p( "DEBUG! dev_id_set =", dev_id_set );
	//log_warnln_d( "DEBUG! dev_id_set_size =", dev_id_set_size );

	for (inx = 0; inx < dev_id_set_size; ++inx){
		//log_warnln_dev_t( "DEBUG! dev_id ", dev_id_set[inx] );

		result = _snapshot_check_data( dev_id_set[inx] );
		if (SUCCESS != result)
			return result;

		log_traceln_dev_t( "device=", dev_id_set[inx] );
	}

	result = _snapshot_New( dev_id_set, dev_id_set_size, &snapshot );
	if (result != SUCCESS){
		log_errorln( "Cannot create snapshot object." );
		return result;
	}
	do{
		//log_warnln_p( "DEBUG! snapshot dev_id_set =", snapshot->dev_id_set );
		//log_warnln_d( "DEBUG! snapshot dev_id_set_size =", snapshot->dev_id_set_size );
		for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
			//log_warnln_dev_t( "DEBUG! snapshot dev_id ", snapshot->dev_id_set[inx] );

			result = _snapshot_add_tracker( snapshot->dev_id_set[inx], cbt_block_size_degree, snapshot->id );
			if (result == -EALREADY){
				log_traceln_d( "Already under tracking device=", snapshot->dev_id_set[inx] );
			result = SUCCESS;
		}
		else if (result != SUCCESS){
				log_errorln_dev_t( "Cannot add device to snapshot tracking. dev_id=", snapshot->dev_id_set[inx] );
			break;
		}
	}
		if (result != SUCCESS)
			break;


		result = tracker_capture_snapshot( snapshot );
		if (SUCCESS != result){
			log_errorln_llx( "Cannot capture snapshot ", snapshot->id );
			break;
		}

		result = snapimage_create_for( snapshot->dev_id_set, snapshot->dev_id_set_size );
			if (result != SUCCESS){
				log_errorln( "Cannot create snapshot image devices." );

			tracker_release_snapshot( snapshot );
			break;
			}

		*psnapshot_id = snapshot->id;
		log_traceln_llx( "snapshot_id=", snapshot->id );
	} while (false);

	if (SUCCESS != result){
		int res;
		container_get( &snapshot->content );
		res = _snapshot_cleanup( snapshot );
		if (res != SUCCESS){
			log_errorln_llx( "Cannot destroy snapshot=", snapshot->id );
			container_push_back( &Snapshots, &snapshot->content );
		}
	}
	return result;
}


int _snapshot_destroy( snapshot_t* snapshot )
{
	int result = SUCCESS;
	size_t inx;

	log_traceln_llx( "snapshot_id=", snapshot->id );
	//
	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
		result = snapimage_stop( snapshot->dev_id_set[inx] );
		if (result != SUCCESS){
			log_errorln_dev_t( "Failed to remove device snapshot image. DevId=", snapshot->dev_id_set[inx] );
		}
	}

	result = tracker_release_snapshot( snapshot );
	if (result != SUCCESS){
		log_errorln_llx( "Failed to release snapshot. snapshot_id=", snapshot->id );
		return result;
	}

	for (inx = 0; inx < snapshot->dev_id_set_size; ++inx){
		result = snapimage_destroy( snapshot->dev_id_set[inx] );
		if (result != SUCCESS){
			log_errorln_dev_t( "Failed to remove device snapshot image. DevId=", snapshot->dev_id_set[inx] );
		}
	}

	return _snapshot_cleanup( snapshot );
}

int snapshot_Destroy( unsigned long long snapshot_id )
{
	int result = SUCCESS;
	snapshot_t* snapshot = NULL;

	result = snapshot_FindById( snapshot_id, &snapshot );
	if (result != SUCCESS){
		log_errorln_llx( "Cannot find snapshot by snapshot_id=", snapshot_id );
		return result;
	}
	container_get( &snapshot->content );
	result = _snapshot_destroy( snapshot );
	if (result != SUCCESS){
		container_push_back( &Snapshots, &snapshot->content );
	}
	return result;
}


