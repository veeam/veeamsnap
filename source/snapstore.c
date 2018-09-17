#include "stdafx.h"
#ifdef SNAPSTORE
#include "snapstore.h"
#include "snapstore_device.h"
#include "zerosectors.h"
/*


void _snapstore_blkinfo_add_ranges( snapshotdata_blkinfo_t* blkinfo, struct ioctl_range_s* ranges, unsigned int ranges_length )
{
	unsigned int inx;
	sector_t range_cnt = 0;

	if ((ranges_length == 0) || (ranges == NULL))
		return;

	for (inx = 0; inx < ranges_length; ++inx)
		range_cnt += sector_from_streamsize( ranges[inx].right - ranges[inx].left );

	spin_lock( &blkinfo->locker );
	blkinfo->limit += range_cnt;
	spin_unlock( &blkinfo->locker );
}
*/
container_t Snapstore;


bool _snapstore_check_halffill( snapstore_t* snapstore, sector_t* fill_status )
{
	if (snapstore->file)
		return snapstore_file_check_halffill( snapstore->file, snapstore->empty_limit, fill_status );

	if (snapstore->mem)
		return snapstore_mem_check_halffill( snapstore->mem, snapstore->empty_limit, fill_status );

	return false;
}

void _snapstore_destroy( snapstore_t* snapstore )
{
	sector_t fill_status;

	log_traceln_uuid( "Destroy snapstore with id=", (&snapstore->id) );

	_snapstore_check_halffill( snapstore, &fill_status );

	if (snapstore->mem != NULL)
		snapstore_mem_destroy( snapstore->mem );
	if (snapstore->file != NULL)
		snapstore_file_destroy( snapstore->file );

	if (snapstore->ctrl_pipe){

		ctrl_pipe_t* pipe = snapstore->ctrl_pipe;
		snapstore->ctrl_pipe = NULL;

		ctrl_pipe_request_terminate( pipe, fill_status );

		ctrl_pipe_put_resource( pipe );
	}
	container_free( &snapstore->content );
}

void _snapstore_destroy_cb( void* resource )
{
	_snapstore_destroy( (snapstore_t*)resource );
}

int snapstore_init( )
{
	int res = SUCCESS;

	res = container_init( &Snapstore, sizeof( snapstore_t ) );
	if (res != SUCCESS){
		log_errorln( "Failed to initialize snapstore" );
	}

	return res;
}

void snapstore_done( )
{
	if (SUCCESS != container_done( &Snapstore )){
		log_errorln( "Container is not empty" );
	};
}

int snapstore_create( uuid_t* id, dev_t snapstore_dev_id, dev_t* dev_id_set, size_t dev_id_set_length )
{
	int res = SUCCESS;
	size_t dev_id_inx;
	snapstore_t* snapstore = NULL;

	if (dev_id_set_length == 0)
		return -EINVAL;

	snapstore = (snapstore_t*)container_new( &Snapstore );
	if (snapstore == NULL)
		return -ENOMEM;

	uuid_copy( &snapstore->id, id );

	log_traceln_uuid( "Create snapstore with id=", (&snapstore->id) );

	snapstore->mem = NULL;
	snapstore->file = NULL;

	snapstore->ctrl_pipe = NULL;
	snapstore->empty_limit = (sector_t)(64 * (1024 * 1024 / SECTOR512)); //by default value
	snapstore->halffilled = false;
	snapstore->overflowed = false;

	if (snapstore_dev_id == 0){
		// memory buffer selected
	} else{
		snapstore_file_t* file = NULL;
		res = snapstore_file_create( snapstore_dev_id, &file );
		if (res != SUCCESS){
			log_errorln_uuid( "Failed to create snapstore file for id=", id );
			return res;
		}
		snapstore->file = file;
	}

	shared_resource_init( &snapstore->shared, snapstore, _snapstore_destroy_cb );


	snapstore_get( snapstore );
	for (dev_id_inx = 0; dev_id_inx < dev_id_set_length; ++dev_id_inx){
		res = snapstore_device_create( dev_id_set[dev_id_inx], snapstore );
		if (res != SUCCESS)
			break;
	}

	if (res != SUCCESS){
		snapstore_device_cleanup( id );
	}
	snapstore_put( snapstore );
	return res;
}


int snapstore_cleanup( uuid_t* id, stream_size_t* filled_bytes )
{
	int res;
	sector_t filled;
	res = snapstore_check_halffill( id, &filled );
	if (res == SUCCESS){
		*filled_bytes = sector_to_streamsize( filled );
		log_traceln_lld( "Snapstore filled ", *filled_bytes );
	}else{
		*filled_bytes = -1;
		log_errorln( "Failed to obtain snapstore data filled size" );
	}

	return snapstore_device_cleanup( id );
}

snapstore_t* _snapstore_find( uuid_t* id )
{
	snapstore_t* result = NULL;
	content_t* content;

	CONTAINER_FOREACH_BEGIN( Snapstore, content )
	{
		snapstore_t* snapstore = (snapstore_t*)content;
		if (uuid_equal( &snapstore->id, id )){
			result = snapstore;
			break;
		}
	}
	CONTAINER_FOREACH_END( Snapstore );

	return result;
}

int snapstore_stretch_initiate( uuid_t* unique_id, ctrl_pipe_t* ctrl_pipe, sector_t empty_limit )
{
	snapstore_t* snapstore = _snapstore_find( unique_id );
	if (NULL == snapstore){
		log_errorln_uuid( "Failed to find snapstore by uuid=", unique_id );
		return -ENODATA;
	}

	snapstore->ctrl_pipe = ctrl_pipe_get_resource( ctrl_pipe );
	snapstore->empty_limit = empty_limit;

	return SUCCESS;
}

int snapstore_add_memory( uuid_t* id, unsigned long long sz )
{
	int res = SUCCESS;
	snapstore_t* snapstore = NULL;


	snapstore = _snapstore_find( id );
	if (snapstore == NULL){
		log_errorln_uuid( "Not found snapstore by id=", id );
		return -ENODATA;
	}

	if (snapstore->file != NULL){
		log_errorln( "Snapstore file already created" );
		return -EINVAL;
	}

	if (snapstore->mem != NULL){
		log_errorln( "Snapstore memory buffer already created" );
		return -EINVAL;
	}

	snapstore->mem = snapstore_mem_create( sz );

	return res;
}


int snapstore_add_file( uuid_t* id, struct ioctl_range_s* ranges, size_t ranges_cnt )
{
	int res = SUCCESS;
	snapstore_t* snapstore = NULL;
	sector_t current_blk_size = 0;

	log_traceln_sz( "ranges count=", ranges_cnt );

	if ((ranges_cnt == 0) || (ranges == NULL))
		return -EINVAL;

	snapstore = _snapstore_find( id );
	if (snapstore == NULL){
		log_errorln_uuid( "Not found snapstore by id=", id );
		return -ENODATA;
	}

	if (snapstore->file == NULL){
		log_errorln( "Snapstore file did not initialized");
		return -EFAULT;
	}

	{
		size_t inx;
		rangelist_t blk_rangelist;
		rangelist_init( &blk_rangelist );

		for (inx = 0; inx < ranges_cnt; ++inx){
			size_t blocks_count = 0;
			sector_t range_offset = 0;
			range_t range;
			range.ofs = sector_from_streamsize( ranges[inx].left );
			range.cnt = sector_from_streamsize( ranges[inx].right ) - range.ofs;

			//log_errorln_range( "range=", range );

			while (range_offset < range.cnt){
				range_t rg;

				rg.ofs = range.ofs + range_offset;
				rg.cnt = min_t( sector_t, (range.cnt - range_offset), (SNAPSTORE_BLK_SIZE - current_blk_size) );

				range_offset += rg.cnt;

				//log_errorln_range( "add rg=", rg );

				res = rangelist_add( &blk_rangelist, &rg );
				if (res != SUCCESS){
					log_errorln( "Failed to add range to rangelist" );
					break;
				}
				current_blk_size += rg.cnt;

				if (current_blk_size == SNAPSTORE_BLK_SIZE){//allocate  block
					res = blk_descr_file_pool_add( &snapstore->file->pool, &blk_rangelist );
					if (res != SUCCESS){
						log_errorln( "Failed initialize block descriptor" );
						break;
					}

					snapstore->halffilled = false;

					//log_errorln( "push block");

					current_blk_size = 0;
					rangelist_init( &blk_rangelist );
					++blocks_count;
				}
				//else{
				//	log_errorln_sect( "current_blk_size=", current_blk_size );
				//}
			}
			if (res != SUCCESS)
				break;

			//log_traceln_sz( "blocks_count=", blocks_count );
		}
	}
	if ((res == SUCCESS) && (current_blk_size != 0)){
		//log_errorln( "Snapstore portion was not ordered by Copy-on-Write block" );
		//res = -EFAULT;
		log_warnln( "Snapstore portion was not ordered by Copy-on-Write block" );
	}
	
#ifdef SNAPDATA_ZEROED
	if ((res == SUCCESS) && (snapstore->file != NULL)){
		snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( snapstore->file->blk_dev_id );
		if (snapstore_device != NULL){
			res = zerosectors_add_ranges( &snapstore_device->zero_sectors, ranges, ranges_cnt );
			if (res != SUCCESS){
				log_errorln( "Cannot add range to zerosectors" );
			}
		}
	}
#endif
	//log_traceln_d( "complete. res=", res );
	return res;
}


void snapstore_order_border( range_t* in, range_t* out )
{
	range_t unorder;

	unorder.ofs = in->ofs & SNAPSTORE_BLK_MASK;
	out->ofs = in->ofs & ~SNAPSTORE_BLK_MASK;
	out->cnt = in->cnt + unorder.ofs;

	unorder.cnt = out->cnt & SNAPSTORE_BLK_MASK;
	if (unorder.cnt != 0)
		out->cnt += (SNAPSTORE_BLK_SIZE - unorder.cnt);
}

blk_descr_unify_t* snapstore_get_empty_block( snapstore_t* snapstore )
{
	blk_descr_unify_t* result = NULL;
	//log_warnln( "<" );

	if (snapstore->overflowed)
		return NULL;

	if (snapstore->file != NULL)
		result = (blk_descr_unify_t*)blk_descr_file_pool_take( &snapstore->file->pool );
	else if (snapstore->mem != NULL){
		void* buffer = snapstore_mem_get_block( snapstore->mem );
		if (buffer){
			result = (blk_descr_unify_t*)blk_descr_mem_pool_add_and_take( &snapstore->mem->pool, buffer );
		}
	}

	if (NULL == result){
		if (snapstore->ctrl_pipe){
			sector_t fill_status;
			_snapstore_check_halffill( snapstore, &fill_status );
			ctrl_pipe_request_overflow( snapstore->ctrl_pipe, -EINVAL, sector_to_streamsize( fill_status ) );
		}
		snapstore->overflowed = true;
	}
	//log_warnln( ">" );
	return result;
}

int snapstore_check_halffill( uuid_t* unique_id, sector_t* fill_status )
{
	snapstore_t* snapstore = _snapstore_find( unique_id );
	if (NULL == snapstore){
		log_errorln_uuid( "Failed to find snapstore by uuid=", unique_id );
		return -ENODATA;
	}

	_snapstore_check_halffill( snapstore, fill_status );

	return SUCCESS;
}


int snapstore_request_store( snapstore_t* snapstore, blk_deferred_request_t* dio_copy_req )
{
	int res = SUCCESS;

	//log_warnln( "<" );

	if (snapstore->ctrl_pipe){
		if (!snapstore->halffilled){
			sector_t fill_status = 0;

			if (_snapstore_check_halffill( snapstore, &fill_status )){
				snapstore->halffilled = true;
				ctrl_pipe_request_halffill( snapstore->ctrl_pipe, sector_to_streamsize( fill_status ) );
			}
		}
	}

	if (snapstore->file)
		res = blk_deferred_request_store_file( snapstore->file->blk_dev, dio_copy_req );
	else if (snapstore->mem)
		res = blk_deffered_request_store_mem( dio_copy_req );
	else
		res = -EINVAL;

	//log_warnln_d( "> res=", res );

	return res;
}

int snapstore_redirect_read( blk_redirect_bio_endio_t* rq_endio, snapstore_t* snapstore, blk_descr_unify_t* blk_descr_ptr, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	int res = SUCCESS;
	sector_t current_ofs = 0;
	sector_t block_ofs = target_pos & SNAPSTORE_BLK_MASK;

	if (snapstore->file){
		range_t* rg;
		blk_descr_file_t* blk_descr = (blk_descr_file_t*)blk_descr_ptr;


		RANGELIST_FOREACH_BEGIN( blk_descr->rangelist, rg )
		{
			if (current_ofs >= rq_count)
				break;

			if (rg->cnt > block_ofs)//read first portion from block
			{
				sector_t pos = rg->ofs + block_ofs;
				sector_t len = min_t( sector_t, (rg->cnt - block_ofs), (rq_count - current_ofs) );

				res = blk_dev_redirect_part( rq_endio, READ, snapstore->file->blk_dev, pos, rq_ofs + current_ofs, len );

				if (res != SUCCESS){
					log_errorln_sect( "Failed to read from snapstore file", pos );
					break;
				}

				current_ofs += len;
				block_ofs = 0;
			}
			else{
				block_ofs -= rg->cnt;
			}
		}
		RANGELIST_FOREACH_END( );

	}
	else if (snapstore->mem){
		blk_descr_mem_t* blk_descr = (blk_descr_mem_t*)blk_descr_ptr;

		res = blk_dev_redirect_memcpy_part( rq_endio, READ, blk_descr->buff + sector_to_size( block_ofs ), rq_ofs, rq_count );
		if (res != SUCCESS){
			log_errorln( "Failed to read from snapstore memory" );
		}else
			current_ofs += rq_count;
	}
	else
		res = -EINVAL;

	if (res != SUCCESS){
		log_errorln_sect( "failed to read by ofs=", target_pos );
	}

	return res;
}

int snapstore_redirect_write( blk_redirect_bio_endio_t* rq_endio, snapstore_t* snapstore, blk_descr_unify_t* blk_descr_ptr, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	int res = SUCCESS;
	sector_t current_ofs = 0;
	sector_t block_ofs = target_pos & SNAPSTORE_BLK_MASK;

	//log_warnln( "<" );

	BUG_ON( NULL == rq_endio );
	BUG_ON( NULL == snapstore );

	if (snapstore->file){
		range_t* rg;
		blk_descr_file_t* blk_descr = (blk_descr_file_t*)blk_descr_ptr;


		RANGELIST_FOREACH_BEGIN( blk_descr->rangelist, rg )
		{
			if (current_ofs >= rq_count)
				break;

			if (rg->cnt > block_ofs)//read first portion from block
			{
				sector_t pos = rg->ofs + block_ofs;
				sector_t len = min_t( sector_t, (rg->cnt - block_ofs), (rq_count - current_ofs) );

				res = blk_dev_redirect_part( rq_endio, WRITE, snapstore->file->blk_dev, pos, rq_ofs + current_ofs, len );

				if (res != SUCCESS){
					log_errorln_sect( "Failed to read from snapstore file", pos );
					break;
				}

				current_ofs += len;
				block_ofs = 0;
			}
			else{
				block_ofs -= rg->cnt;
			}
		}
		RANGELIST_FOREACH_END( );

	}
	else if (snapstore->mem){
		blk_descr_mem_t* blk_descr = (blk_descr_mem_t*)blk_descr_ptr;

		res = blk_dev_redirect_memcpy_part( rq_endio, WRITE, blk_descr->buff + sector_to_size( block_ofs ), rq_ofs, rq_count );
		if (res != SUCCESS){
			log_errorln( "Failed to read from snapstore memory" );
		}
		else
			current_ofs += rq_count;
	}
	else{
		log_errorln( "Invalid type of snapstore device" );
		res = -EINVAL;
	}


	if (res != SUCCESS){
		log_errorln_sect( "failed to read by ofs=", target_pos );
	}
	//else{
	//	log_traceln_sect( "was read=", current_ofs );
	//}

	return res;
}

/*

int snapstore_find_by_dev( dev_t dev_id, snapstore_device_t** psnapstore_device )
{
	int res = -ENODATA;
	content_t* content;
	*psnapstore_device = NULL;

	CONTAINER_FOREACH_BEGIN( Snapstores, content )
	{
		snapstore_t* snapstore = (snapstore_t*)(content);

		res = snapstore_device_find_by_dev_id( devices, dev_id, psnapstore_device );
		if (SUCCESS == res)
			break;
	}
	CONTAINER_FOREACH_END( Snapstores );

	return res;
}
*/

#endif //SNAPSTORE
