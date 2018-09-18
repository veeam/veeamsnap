#include "stdafx.h"

#ifdef SNAPSTORE
#include "snapstore_device.h"
#include "snapstore.h"
#include "blk_util.h"

container_t SnapstoreDevices;

static inline void _snapstore_device_descr_write_lock( snapstore_device_t* snapstore_device )
{
//	log_errorln( ".");
//	down_write( &snapstore_device->store_block_map_locker );
	mutex_lock( &snapstore_device->store_block_map_locker );
}
static inline void _snapstore_device_descr_write_unlock( snapstore_device_t* snapstore_device )
{
//	log_errorln( "." );
//	up_write( &snapstore_device->store_block_map_locker );
	mutex_unlock( &snapstore_device->store_block_map_locker );
}


int snapstore_device_init( void )
{
	int res = SUCCESS;

	res = container_init( &SnapstoreDevices, sizeof( snapstore_device_t ) );
	if (res != SUCCESS)
		return res;


	return res;
}

void snapstore_device_done( void )
{
	if (SUCCESS != container_done( &SnapstoreDevices )){
		snapstore_device_t* snapstore_device;

		log_errorln( "Container is not empty" );

		while (NULL != (snapstore_device = (snapstore_device_t*)container_get_first( &SnapstoreDevices ))){
			snapstore_device_put_resource( snapstore_device );
		}
	};
}

snapstore_device_t* snapstore_device_find_by_dev_id( dev_t dev_id )
{
	content_t* content;
	snapstore_device_t* result = NULL;

	CONTAINER_FOREACH_BEGIN( SnapstoreDevices, content )
	{
		snapstore_device_t* snapstore_device = (snapstore_device_t*)(content);

		if (dev_id == snapstore_device->dev_id){
			result = snapstore_device;
			//_container_del( &SnapstoreDevices, content );
			break;
		}
	}
	CONTAINER_FOREACH_END( SnapstoreDevices );

	return result;
}

snapstore_device_t* _snapstore_device_get_by_snapstore_id( veeam_uuid_t* id )
{
	content_t* content;
	snapstore_device_t* result = NULL;

	CONTAINER_FOREACH_BEGIN( SnapstoreDevices, content )
	{
		snapstore_device_t* snapstore_device = (snapstore_device_t*)(content);

		if (veeam_uuid_equal( id, &snapstore_device->snapstore->id )){
			result = snapstore_device;
			_container_del( &SnapstoreDevices, content );
			//log_warnln( "Found" );
			break;
		}
	}
	CONTAINER_FOREACH_END( SnapstoreDevices );
	return result;
}


void _snapstore_device_destroy( snapstore_device_t* snapstore_device )
{
	log_traceln_uuid( "snapstore_id=", (&snapstore_device->snapstore->id) );

	blk_descr_array_done( &snapstore_device->store_block_map );

	if (snapstore_device->orig_blk_dev != NULL){
		blk_dev_close( snapstore_device->orig_blk_dev );
	}

#ifdef SNAPDATA_ZEROED
	rangevector_done( &snapstore_device->zero_sectors );
#endif
	if (snapstore_device->snapstore){
		snapstore_put( snapstore_device->snapstore );
		snapstore_device->snapstore = NULL;
	}
	content_free( &snapstore_device->content );
}

void snapstore_device_free_cb( void* resource )
{
	snapstore_device_t* snapstore_device = (snapstore_device_t*)resource;

	_snapstore_device_destroy( snapstore_device );
}

int snapstore_device_cleanup( veeam_uuid_t* id )
{
	int result = SUCCESS;
	snapstore_device_t* snapstore_device = NULL;
	//log_warnln( "." );
	while (NULL != (snapstore_device = _snapstore_device_get_by_snapstore_id( id ))){
		log_traceln_dev_t( "Cleanup  snapstore device for ", snapstore_device->dev_id );

		snapstore_device_put_resource( snapstore_device );
	}
	return result;
}

int snapstore_device_create( dev_t dev_id, snapstore_t* snapstore )
{
	int res = SUCCESS;
	snapstore_device_t* snapstore_device;

	snapstore_device = (snapstore_device_t*)content_new( &SnapstoreDevices );
	if (NULL == snapstore_device)
		return -ENOMEM;

	snapstore_device->dev_id = dev_id;

	//log_traceln_uuid( "snapstore_id=", (&snapstore->id) );

	res = blk_dev_open( dev_id, &snapstore_device->orig_blk_dev );
	if (res != SUCCESS){
		log_errorln_dev_t( "Failed to open original device ", dev_id );
		return res;
	}

	shared_resource_init( &snapstore_device->shared, snapstore_device, snapstore_device_free_cb );

	snapstore_device->snapstore = NULL;
	snapstore_device->err_code = SUCCESS;
	snapstore_device->corrupted = false;
	atomic_set( &snapstore_device->req_failed_cnt, 0 );

	do{
		blk_descr_array_index_t blocks_count;
		{
			sector_t sect_cnt = blk_dev_get_capacity( snapstore_device->orig_blk_dev );
			if (sect_cnt & SNAPSTORE_BLK_MASK)
				sect_cnt += SNAPSTORE_BLK_SIZE;
			blocks_count = (blk_descr_array_index_t)(sect_cnt >> SNAPSTORE_BLK_SHIFT);
		}
		res = blk_descr_array_init( &snapstore_device->store_block_map, 0, blocks_count );
		if (res != SUCCESS){
			log_errorln_sect( "Failed to initialize block description array for blocks count ", blocks_count );
			break;
		}
		//init_rwsem( &snapstore_device->store_block_map_locker );
		mutex_init( &snapstore_device->store_block_map_locker );

#ifdef SNAPDATA_ZEROED
		rangevector_init( &snapstore_device->zero_sectors, true );
#endif

		snapstore_device->snapstore = snapstore_get( snapstore );
		log_traceln_uuid( "snapstore_id=", (&snapstore_device->snapstore->id) );

		container_push_back( &SnapstoreDevices, &snapstore_device->content );
		snapstore_device_get_resource( snapstore_device );

		//log_warnln( "snapstore_device add to SnapstoreDevices" );
	} while (false);

	if (res != SUCCESS)
		_snapstore_device_destroy( snapstore_device );

	return res;
}

bool _snapstore_device_is_block_stored( snapstore_device_t* snapstore_device, blk_descr_array_index_t block_index )
{
	blk_descr_array_el_t blk_descr;
	int res = blk_descr_array_get( &snapstore_device->store_block_map, block_index, &blk_descr );
	if (res == SUCCESS)
		return true;
	if (res == -ENODATA)
		return false;

	log_errorln_d( "Failed to get element from block description array. err=", res );
	return false;
}
/*

int _snapstore_device_set_block_stored( snapstore_device_t* snapstore_device, blk_deferred_t* dio )
{
	int res = SUCCESS;
	_snapstore_device_descr_read_unlock( snapstore_device );
	return res;
}*/

int snapstore_device_add_request( snapstore_device_t* snapstore_device, blk_descr_array_index_t block_index, blk_deferred_request_t** dio_copy_req )
{
	int res = SUCCESS;
	blk_descr_unify_t* blk_descr = NULL;
	blk_deferred_t* dio = NULL;
	bool req_new = false;

	//log_warnln( "<" );


	blk_descr = snapstore_get_empty_block( snapstore_device->snapstore );
	if (blk_descr == NULL){
		log_errorln( "Failed to allocate next block" );
		return -ENODATA;
	}

	res = blk_descr_array_set( &snapstore_device->store_block_map, block_index, blk_descr );
	if (res != SUCCESS){
		//blk_descr_write_unlock( blk_descr );
		log_errorln_d( "Failed to set element to block description array. err=", res );
		return res;
	}

	if (*dio_copy_req == NULL){
		*dio_copy_req = blk_deferred_request_new( );
		if (*dio_copy_req == NULL){
			log_errorln( "Failed to allocate deferred request" );
			return -ENOMEM;

		}
		req_new = true;
	}

	do{
		dio = blk_deferred_alloc( block_index, blk_descr );
		if (dio == NULL){
			log_errorln_sz( "dio alloc failed. block_index=", block_index );
			res = -ENOMEM;
			break;
		}

		res = blk_deferred_request_add( *dio_copy_req, dio );
		if (res != SUCCESS){
			log_errorln( "Failed to add deferred request" );
		}
	} while (false);

	if (res != SUCCESS){
		if (dio != NULL){
			blk_deferred_free( dio );
			dio = NULL;
		}
		if (req_new){
			blk_deferred_request_free( *dio_copy_req );
			*dio_copy_req = NULL;
		}
	}

	//log_warnln( ">" );
	return res;
}

int snapstore_device_prepare_requests( snapstore_device_t* snapstore_device, range_t* copy_range, blk_deferred_request_t** dio_copy_req )
{
	int res = SUCCESS;
	blk_descr_array_index_t inx = 0;
	blk_descr_array_index_t first = (blk_descr_array_index_t)(copy_range->ofs >> SNAPSTORE_BLK_SHIFT);
	blk_descr_array_index_t last = (blk_descr_array_index_t)((copy_range->ofs + copy_range->cnt - 1) >> SNAPSTORE_BLK_SHIFT);

	for (inx = first; inx <= last; inx++){
		if (_snapstore_device_is_block_stored( snapstore_device, inx ))
		{
			//log_traceln_sz( "Already stored block # ", inx );
		}else{

			res = snapstore_device_add_request( snapstore_device, inx, dio_copy_req );
			if ( res != SUCCESS){
				log_errorln_d( "Failed to create copy request. res=", res );
				break;
			}
		}
	}
	if (res != SUCCESS){
		snapstore_device_set_corrupted( snapstore_device, res );
	}

	return res;
}
/*

int _snapstore_device_request_stored( snapstore_device_t* snapstore_device, blk_deferred_request_t* dio_copy_req )
{
	int res = SUCCESS;
	//log_warnln( "<" );
#ifdef BLK_DEFER_LIST
	if (!list_empty( &dio_copy_req->dios )){
		struct list_head* _list_head;
		list_for_each( _list_head, &dio_copy_req->dios ){
			blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );
#else//BLK_DEFER_LIST
	int dio_inx;
	for (dio_inx = 0; dio_inx < dio_copy_req->dios_cnt; ++dio_inx){
		blk_deferred_t* dio = dio_copy_req->dios[dio_inx];
		{
#endif//BLK_DEFER_LIST

			res = _snapstore_device_set_block_stored( snapstore_device, dio );
			if (res != SUCCESS)
				break;
		}
	}
	//log_warnln( ">" );
	return res;
}
*/


int snapstore_device_store( snapstore_device_t* snapstore_device, blk_deferred_request_t* dio_copy_req )
{
	int res;

	//log_warnln( "<" );

	res = snapstore_request_store( snapstore_device->snapstore, dio_copy_req );
	/*if (res == SUCCESS){
		res = _snapstore_device_request_stored( snapstore_device, dio_copy_req );
	}*/

	if (res != SUCCESS)
		snapstore_device_set_corrupted( snapstore_device, res );

	return res;
}


int snapstore_device_read( snapstore_device_t* snapstore_device, blk_redirect_bio_endio_t* rq_endio )
{
	int res = SUCCESS;

	blk_descr_array_index_t block_index;
	blk_descr_array_index_t block_index_last;
	blk_descr_array_index_t block_index_first;

	sector_t blk_ofs_start = 0;         //device range start
	sector_t blk_ofs_count = 0;         //device range length

	range_t rq_range;

#ifdef SNAPDATA_ZEROED
	rangevector_t* zero_sectors = NULL;
	if (get_zerosnapdata( ))
		zero_sectors = &snapstore_device->zero_sectors;
#endif //SNAPDATA_ZEROED

	//log_warnln( "<" );

	if (snapstore_device_is_corrupted( snapstore_device ))
		return -ENODATA;

	rq_range.cnt = sector_from_size( bio_bi_size( rq_endio->bio ) );
	rq_range.ofs = bio_bi_sector( rq_endio->bio );

	//log_errorln_sect( "pos= ", rq_pos );
	//log_errorln_sect( "len= ", rq_len );

	if (!bio_has_data( rq_endio->bio )){
		log_warnln_sz( "bio empty. flags=", rq_endio->bio->bi_flags );

		blk_redirect_complete( rq_endio, SUCCESS );
		return SUCCESS;
	}

	block_index_first = (blk_descr_array_index_t)(rq_range.ofs >> SNAPSTORE_BLK_SHIFT);
	block_index_last = (blk_descr_array_index_t)((rq_range.ofs + rq_range.cnt - 1) >> SNAPSTORE_BLK_SHIFT);

	_snapstore_device_descr_write_lock( snapstore_device );
	for (block_index = block_index_first; block_index <= block_index_last; ++block_index){
		int status;
		blk_descr_unify_t* blk_descr = NULL;

		blk_ofs_count = min_t( sector_t,
			(((sector_t)(block_index + 1)) << SNAPSTORE_BLK_SHIFT) - (rq_range.ofs + blk_ofs_start),
			rq_range.cnt - blk_ofs_start );

		status = blk_descr_array_get( &snapstore_device->store_block_map, block_index, &blk_descr );
		if (SUCCESS != status){
			if (-ENODATA == status)
				blk_descr = NULL;
			else{
				res = status;
				log_errorln( "Failed to get snapstore block" );
				break;
			}
		}
		if (blk_descr ){
			//push snapstore read
			res = snapstore_redirect_read( rq_endio, snapstore_device->snapstore, blk_descr, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );
			if (res != SUCCESS){
				log_errorln( "Failed to redirect read request to snapstore" );
				break;
			}
		}
		else{

#ifdef SNAPDATA_ZEROED
			//device read with zeroing
			if (zero_sectors)
				res = blk_dev_redirect_read_zeroed( rq_endio, snapstore_device->orig_blk_dev, rq_range.ofs, blk_ofs_start, blk_ofs_count, zero_sectors );
			else
#endif
			res = blk_dev_redirect_part( rq_endio, READ, snapstore_device->orig_blk_dev, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );

			if (res != SUCCESS){
				log_errorln_dev_t( "Failed to redirect read request to original device ", snapstore_device->dev_id );
				break;
			}
		}

		blk_ofs_start += blk_ofs_count;
	}

	_snapstore_device_descr_write_unlock( snapstore_device );

	if (res == SUCCESS){
		if (atomic64_read( &rq_endio->bio_endio_count ) > 0ll) //async direct access needed
			blk_dev_redirect_submit( rq_endio );
		else
			blk_redirect_complete( rq_endio, res );
	}
	else{
		log_errorln( "Failed to read" );
		log_errorln_sect( "pos= ", rq_range.ofs );
		log_errorln_sect( "len= ", rq_range.cnt );
	}

	//log_warnln( ">" );

	return res;
}

int _snapstore_device_copy_on_write( snapstore_device_t* snapstore_device, range_t* rq_range )
{
	int res = SUCCESS;
	blk_deferred_request_t* dio_copy_req = NULL;

	//log_warnln( "<" );
	_snapstore_device_descr_read_lock( snapstore_device );
	do{
		res = snapstore_device_prepare_requests( snapstore_device, rq_range, &dio_copy_req );
		if (res != SUCCESS){
			log_errorln_d( "Failed to add range for COW. error=", res );
			break;
		}

		if (NULL == dio_copy_req)
			break;//nothing to copy

		res = blk_deferred_request_read_original( snapstore_device->orig_blk_dev, dio_copy_req );
		if (res != SUCCESS){
			log_errorln_d( "Failed to read data for COW. err=", res );
			break;
		}
		res = snapstore_device_store( snapstore_device, dio_copy_req );
		if (res != SUCCESS){
			log_errorln_d( "Failed to write data for COW. err=", res );
			break;
		}
	} while (false);
	_snapstore_device_descr_read_unlock( snapstore_device );

	if (dio_copy_req){
		if (res == -EDEADLK)
			blk_deferred_request_deadlocked( dio_copy_req );
		else
			blk_deferred_request_free( dio_copy_req );
	}
	//log_warnln( ">" );
	return res;
}


int snapstore_device_write( snapstore_device_t* snapstore_device, blk_redirect_bio_endio_t* rq_endio )
{
	int res = SUCCESS;

	blk_descr_array_index_t block_index;
	blk_descr_array_index_t block_index_last;
	blk_descr_array_index_t block_index_first;

	sector_t blk_ofs_start = 0;         //device range start
	sector_t blk_ofs_count = 0;         //device range length

	range_t rq_range;

	//log_warnln( "<" );
	BUG_ON( NULL == snapstore_device );
	BUG_ON( NULL == rq_endio );
	BUG_ON( NULL == rq_endio->bio );

	if (snapstore_device_is_corrupted( snapstore_device ))
		return -ENODATA;

	rq_range.cnt = sector_from_size( bio_bi_size( rq_endio->bio ) );
	rq_range.ofs = bio_bi_sector( rq_endio->bio );

	if (!bio_has_data( rq_endio->bio )){
		log_warnln_sz( "bio empty. flags=", rq_endio->bio->bi_flags );
		blk_redirect_complete( rq_endio, SUCCESS );
		return SUCCESS;
	}

	// do copy to snapstore previously
	res = _snapstore_device_copy_on_write( snapstore_device, &rq_range );

	block_index_first = (blk_descr_array_index_t)(rq_range.ofs >> SNAPSTORE_BLK_SHIFT);
	block_index_last = (blk_descr_array_index_t)((rq_range.ofs + rq_range.cnt - 1) >> SNAPSTORE_BLK_SHIFT);


	for (block_index = block_index_first; block_index <= block_index_last; ++block_index){
		int status;
		blk_descr_unify_t* blk_descr = NULL;

		blk_ofs_count = min_t( sector_t,
			(((sector_t)(block_index + 1)) << SNAPSTORE_BLK_SHIFT) - (rq_range.ofs + blk_ofs_start),
			rq_range.cnt - blk_ofs_start );

		status = blk_descr_array_get( &snapstore_device->store_block_map, block_index, &blk_descr );
		if (SUCCESS != status){
			if (-ENODATA == status)
				blk_descr = NULL;
			else{
				res = status;
				log_errorln( "Failed to get snapstore block" );
				break;
			}
		}
		if (blk_descr == NULL){
			log_errorln_sect( "Block is not copied. offset=", rq_range.ofs );
			res = -EIO;
			break;
		}

		res = snapstore_redirect_write( rq_endio, snapstore_device->snapstore, blk_descr, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );
		if (res != SUCCESS){
			log_errorln( "Failed to redirect read request to snapstore" );
			break;
		}

		blk_ofs_start += blk_ofs_count;
	}
	if (res == SUCCESS){
		if (atomic64_read( &rq_endio->bio_endio_count ) > 0){ //async direct access needed
			blk_dev_redirect_submit( rq_endio );
		}
		else{
			blk_redirect_complete( rq_endio, res );
		}
	}
	else{
		log_errorln( "Failed to write" );
		log_errorln_sect( "pos= ", rq_range.ofs );
		log_errorln_sect( "len= ", rq_range.cnt );

		snapstore_device_set_corrupted( snapstore_device, res );
	}

	//log_warnln_d( "> res=", res );
	return res;
}

bool snapstore_device_is_corrupted( snapstore_device_t* snapstore_device )
{
	if (snapstore_device == NULL)
		return true;

	if (snapstore_device->corrupted){
		if (0 == atomic_read( &snapstore_device->req_failed_cnt )){
			log_errorln_dev_t( "Snapshot is corrupted for device ", snapstore_device->dev_id );
		}
		atomic_inc( &snapstore_device->req_failed_cnt );
		return true;
	}

	return false;
}

void snapstore_device_set_corrupted( snapstore_device_t* snapstore_device, int err_code )
{
	if (!snapstore_device->corrupted){
		atomic_set( &snapstore_device->req_failed_cnt, 0 );
		snapstore_device->corrupted = true;
		snapstore_device->err_code = err_code;

		log_errorln_d( "Now snapshot corrupted. error=", (0 - err_code) );
	}
}

void snapstore_device_print_state( snapstore_device_t* snapstore_device )
{
	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );

	//pr_warn( "sectors: copy_write=%lld \n",
	//	(long long int)atomic64_read( &snapshotdata->state_sectors_write ) );

	//wrote_mb = (unsigned long)(atomic64_read( &snapshotdata->state_sectors_write ) >> (20 - SECTOR512_SHIFT));

	//pr_warn( "bytes: copy_write=%lu MiB \n", wrote_mb );

	if (snapstore_device->corrupted){
		pr_warn( "Corrupted. Failed request count: %d MiB \n", atomic_read( &snapstore_device->req_failed_cnt ) );
	}
}

int snapstore_device_errno( dev_t dev_id, int* p_err_code )
{
	snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( dev_id );
	if (snapstore_device == NULL)
		return -ENODATA;

	*p_err_code = snapstore_device->err_code;
	return SUCCESS;
}

#endif //SNAPSTORE