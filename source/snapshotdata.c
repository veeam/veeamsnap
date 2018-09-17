#include "stdafx.h"

#ifndef SNAPSTORE

#include "snapshotdata.h"
#include "blk_util.h"
#include "blk_direct.h"
#include "snapshotdata_blkinfo.h"
#include "zerosectors.h"



static container_t Snapshotdatas;

int snapshotdata_Init( void )
{
	return container_init( &Snapshotdatas, sizeof( snapshotdata_t ) );
}

int _snapshotdata_destroy( snapshotdata_t* snapshotdata );

int snapshotdata_Destroy( snapshotdata_t* snapshotdata )
{
	int status;
	container_get( &snapshotdata->content );

	status = _snapshotdata_destroy( snapshotdata );
	if ((status != SUCCESS) && (status != -EALREADY)){
		log_traceln_dev_t( "Failed to remove snapshot data for device=", snapshotdata->dev_id );
		return status;
	}

	content_free( &snapshotdata->content );
	return SUCCESS;

}

int _snapshotdata_Delete( uuid_t* id )
{
	int result = -ENODATA;

	log_traceln_uuid( "Removing snapshot data ", id );

	while(true){
		content_t* content;
		snapshotdata_t* snapshotdata = NULL;
		CONTAINER_FOREACH_BEGIN( Snapshotdatas, content )
		{
			if ( snapshotdata_shared_id_equal( id, ((snapshotdata_t*)content)->shared ) ){
				snapshotdata = (snapshotdata_t*)content;
				break;
			}
		}
		CONTAINER_FOREACH_END( Snapshotdatas );

		if (snapshotdata == NULL)
			break;
		{
			int status = snapshotdata_Destroy( snapshotdata );
			if (result == -ENODATA)
				result = status;
		}
	}

	return result;
}

int snapshotdata_shared_cleanup( uuid_t* id )
{
	int res = _snapshotdata_Delete( id );
	if (res == -ENODATA){
		log_traceln_uuid( "Snapshot data already absent ", id );
		return -EALREADY;
	}
	if (res != SUCCESS)
		log_errorln_uuid( "Failed to delete snapshot data ", id );

	return res;
}

int _CleanuAll_cb( content_t* pCnt, void* param )
{
	snapshotdata_t* snapshotdata = (snapshotdata_t*)pCnt;

	_snapshotdata_destroy( snapshotdata );

	return ENODATA;	//continue
}


int snapshotdata_DeleteAll( void )
{
	int result = SUCCESS;

	log_traceln( "Removing all snapshot datas" );

	result = container_enum_and_free( &Snapshotdatas, _CleanuAll_cb, NULL );
	if (result != SUCCESS)
		log_traceln_d( "Some snapshot info data isn`t removed. status=", result );

	return result;
}


int snapshotdata_Done( void )
{
	int result;

	result = snapshotdata_DeleteAll( );

	if (result == SUCCESS){
		if (SUCCESS != (result = container_done( &Snapshotdatas ))){
			log_errorln( "Container is not empty" );
		};
	}
	return result;
}


typedef struct _FindByDevId_cb_s
{
	dev_t dev_id;
	snapshotdata_t* snapshotdata;
}_FindByDevId_cb_t;

int _FindByDevId_cb( content_t* pCnt, void* param )
{
	snapshotdata_t* snapshotdata = (snapshotdata_t*)pCnt;
	_FindByDevId_cb_t* p_param = (_FindByDevId_cb_t*)param;

	if (p_param->dev_id == snapshotdata->dev_id){
		p_param->snapshotdata = snapshotdata;
		return SUCCESS;
	}

	return ENODATA;	//continue
}

int snapshotdata_FindByDevId( dev_t dev_id, snapshotdata_t** psnapshotdata )
{
	int result = SUCCESS;
	_FindByDevId_cb_t param;
	param.dev_id = dev_id;
	param.snapshotdata = NULL;

	result = container_enum( &Snapshotdatas, _FindByDevId_cb, &param );

	if (param.snapshotdata != NULL){
		if (psnapshotdata != NULL){
			*psnapshotdata = param.snapshotdata;
			log_traceln_dev_t( "Snapshot data for device successfully found. dev_id=", dev_id );
		}
	}
	else{
		result = -ENODATA;
		log_traceln_dev_t( "Snapshot data for device not found. dev_id=", dev_id );
	}
	return result;
}

int snapshotdata_Errno( dev_t dev_id, int* p_err_code )
{
	int res;
	snapshotdata_t* snapshotdata = NULL;

	res = snapshotdata_FindByDevId( dev_id, &snapshotdata );
	if (res != SUCCESS)
		return res;

	*p_err_code = snapshotdata->err_code;
	return SUCCESS;
}

int _snapshotdata_create( snapshotdata_t* snapshotdata, sector_t info_dev_count_sect )
{
	int res;
	sparse_array_index_t acc_map_size;

	acc_map_size = (sparse_array_index_t)(info_dev_count_sect >> SNAPSHOTDATA_BLK_SHIFT);
	if ((info_dev_count_sect & SNAPSHOTDATA_BLK_MASK))
		acc_map_size += 1;

	log_traceln_sect( "Accordance map limit size =", acc_map_size );

	res = sparse_array_1lv_init( &snapshotdata->acc_map, 0, acc_map_size );
	if (res != SUCCESS){
		log_errorln( "Failed to initialize sparse array accordance_map." );
		return res;
	}
	res = sparse_array_1lv_init( &snapshotdata->write_acc_map, 0, acc_map_size );
	if (res != SUCCESS){
		log_errorln( "Failed to initialize sparse array accordance_map." );
		return res;
	}
#ifdef SNAPDATA_ZEROED
	if (get_zerosnapdata( )){
		log_traceln( "Zero ranges bitmap create" );
		rangevector_init( &snapshotdata->zero_sectors, true );
	}
#endif

	atomic64_set( &snapshotdata->state_sectors_write, 0 );

	return res;
}



int _snapshotdata_check_io_compatibility( dev_t dev_id, struct block_device* snapdata_blk_dev, bool* is_compatibility );

int snapshotdata_add_dev( snapshotdata_shared_t* shared, dev_t dev_id )
{
	int res = SUCCESS;
	snapshotdata_t* snapdata = NULL;
	snapshotdata_file_t* file = NULL;

	log_traceln_dev_t( " device=", dev_id );

	if (shared->type == SNAPSHOTDATA_TYPE_COMMON){
		snapshotdata_common_t* comm_disk = (snapshotdata_common_t*)shared;
		file = &comm_disk->file;
	}
	if (shared->type == SNAPSHOTDATA_TYPE_STRETCH){
		snapshotdata_stretch_t* stretch = (snapshotdata_stretch_t*)shared;
		file = &stretch->file;
	}

	if (file != NULL){ //check snapshotdata io limit compatibility
		bool is_compatible = false;

		res = _snapshotdata_check_io_compatibility( dev_id, file->blk_dev, &is_compatible );
		if (res != SUCCESS){
			return res;
		}
		if (!is_compatible){
			log_errorln_dev_t( "Incompatible physical block size for device ", dev_id );
			return -EPERM;
		}
	}

	snapdata = (snapshotdata_t*)content_new( &Snapshotdatas );
	if (snapdata == NULL)
		return -ENOMEM;
	do{
		blk_dev_info_t info_dev;
		snapdata->dev_id = dev_id;
		res = blk_dev_get_info( snapdata->dev_id, &info_dev );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot obtain info about original device=.", dev_id );
			break;
		}

		res = _snapshotdata_create( snapdata, info_dev.count_sect );
		if (res != SUCCESS)
			break;
	} while (false);

	if (res == SUCCESS){
		snapdata->shared = snapshotdata_shared_get_resource( shared );

#ifdef SNAPDATA_ZEROED
		if (file != NULL)
			zerosectors_add_file( &snapdata->zero_sectors, snapdata->dev_id, file );
#endif
		container_push_back( &Snapshotdatas, &snapdata->content );
	}
	else{
		if (snapdata != NULL)
			content_free( &snapdata->content );
	}
	return res;
}

int _snapshotdata_destroy( snapshotdata_t* snapshotdata )
{
	if (snapshotdata == NULL)
		return -ENODATA;

	{
		unsigned long wrote_mb = (unsigned long)(atomic64_read( &snapshotdata->state_sectors_write ) >> (20 - SECTOR512_SHIFT));
		log_traceln_ld( "Snapshot data filled MiB ", wrote_mb );
	}

	if (snapshotdata->shared == NULL){
		log_errorln( "Snapshotdata already destroyed." );
		return -EALREADY;
	}

    {
		snapshotdata_shared_t* resource = snapshotdata->shared;
		snapshotdata->shared = NULL;
		snapshotdata_shared_put_resource( resource );
	}

	sparse_array_1lv_done( &snapshotdata->acc_map );
	sparse_array_1lv_done( &snapshotdata->write_acc_map );

#ifdef SNAPDATA_ZEROED
	if (get_zerosnapdata( )){
		rangevector_done( &snapshotdata->zero_sectors );
		log_traceln( "Zero sectors bitmap destroy." );
	}
#endif

	return SUCCESS;
}


int _snapshotdata_add_range( snapshotdata_file_t* file, snapshotdata_blkinfo_t* blkinfo, range_t* range )
{
	int res = rangevector_add( &file->dataranges, range );
	if (res == SUCCESS)
		snapshotdata_blkinfo_add( blkinfo, range->cnt );
	return res;
}

int snapshotdata_add_ranges( snapshotdata_file_t* file, snapshotdata_blkinfo_t* blkinfo, struct ioctl_range_s* ranges, unsigned int ranges_length )
{
	unsigned int inx;

	if ((ranges_length == 0) || (ranges == NULL))
		return -EINVAL;

	for (inx = 0; inx < ranges_length; ++inx){
		int res = SUCCESS;
		range_t range;
		range.ofs = sector_from_streamsize( ranges[inx].left );
		range.cnt = sector_from_streamsize( ranges[inx].right ) - range.ofs;

		res = _snapshotdata_add_range( file, blkinfo, &range );
		if (res != SUCCESS){
			log_errorln( "Cannot add range" );
			return res;
		}
	}
	return SUCCESS;
}


int _snapshotdata_write_direct( snapshotdata_t* snapshotdata, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	int res = SUCCESS;
	atomic64_add( blk_cnt, &snapshotdata->state_sectors_write );
	switch (snapshotdata->shared->type){
	case SNAPSHOTDATA_TYPE_STRETCH: res = snapshotdata_file_direct_write( &snapshotdata->stretch->file, arr_ofs, arr, blk_ofs, blk_cnt ); break;
	case SNAPSHOTDATA_TYPE_COMMON: res = snapshotdata_file_direct_write( &snapshotdata->common->file, arr_ofs, arr, blk_ofs, blk_cnt ); break;
	case SNAPSHOTDATA_TYPE_MEM:	res = snapshotdata_buffer_write_direct( &snapshotdata->mem->buffer, arr_ofs, arr, blk_ofs, blk_cnt ); break;
	default: res = -EINVAL;
	}
	return res;
}

int _snapshotdata_write_dio_direct( snapshotdata_t* snapshotdata, blk_deferred_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	int res = SUCCESS;
	atomic64_add( blk_cnt, &snapshotdata->state_sectors_write );

	switch (snapshotdata->shared->type){
	case SNAPSHOTDATA_TYPE_STRETCH:	res = snapshotdata_file_direct_write_dio( &snapshotdata->stretch->file, dio_req, arr_ofs, arr, blk_ofs, blk_cnt ); break;
	case SNAPSHOTDATA_TYPE_COMMON:res = snapshotdata_file_direct_write_dio( &snapshotdata->common->file, dio_req, arr_ofs, arr, blk_ofs, blk_cnt ); break;
	case SNAPSHOTDATA_TYPE_MEM:	res = snapshotdata_buffer_write_dio_direct( &snapshotdata->mem->buffer, dio_req, arr_ofs, arr, blk_ofs, blk_cnt ); break;
	default: res = -EINVAL;
	}
	return res;
}

snapshotdata_blkinfo_t* _shapshotada_get_blkinfo( snapshotdata_t* snapshotdata )
{
	snapshotdata_blkinfo_t* blkinfo = NULL;
	switch (snapshotdata->shared->type){
	case SNAPSHOTDATA_TYPE_STRETCH: blkinfo = &snapshotdata->stretch->blkinfo; break;
	case SNAPSHOTDATA_TYPE_COMMON: blkinfo = &snapshotdata->common->blkinfo; break;
	case SNAPSHOTDATA_TYPE_MEM: blkinfo = &snapshotdata->mem->blkinfo; break;
	}
	return blkinfo;
}

int _snapshotdata_write_dio_request_to( snapshotdata_t* snapshotdata, sparse_arr1lv_t* to_map, blk_deferred_request_t* dio_req )
{
	int res = -EFAULT;
	int dio_inx;
	sector_t block_inx;
	sector_t blk_pos;
	snapshotdata_blkinfo_t* blkinfo = _shapshotada_get_blkinfo( snapshotdata );
	if (blkinfo == NULL)
		return -EINVAL;

	if (snapshotdata->shared->type == SNAPSHOTDATA_TYPE_STRETCH)
		snapshotdata_stretch_check_halffill( snapshotdata->stretch, blkinfo );

	for (dio_inx = 0; dio_inx < dio_req->dios_cnt; ++dio_inx){
		blk_deferred_t* dio = dio_req->dios[dio_inx];

		res = snapshotdata_blkinfo_pos_increment( blkinfo, dio->sect.cnt, &blk_pos );
		if (res != SUCCESS){
			if (snapshotdata->shared->type == SNAPSHOTDATA_TYPE_STRETCH)
				snapshotdata_stretch_overflow( snapshotdata->stretch, res, blkinfo );

			log_errorln_sect( "blk_pos + size = ", (blkinfo->pos + dio->sect.cnt) );
			log_errorln_sect( "blk_cnt = ", blkinfo->limit );
			log_errorln( "Cannot store data to snapshot. Not enough space." );
			return -ENODATA;
		}

		res = _snapshotdata_write_dio_direct( snapshotdata, dio_req, 0, dio->buff, blk_pos, dio->sect.cnt );
		if (res != SUCCESS){
			log_errorln( "Failed to call _snapshotdata_write_direct." );
			return res;
		}

		for (block_inx = 0; block_inx < dio->sect.cnt; block_inx += SNAPSHOTDATA_BLK_SIZE){
			res = sparse_array_1lv_set(
				to_map,
				(dio->sect.ofs + block_inx) >> SNAPSHOTDATA_BLK_SHIFT,
				(sparse_array_el_t)((blk_pos + block_inx) >> SNAPSHOTDATA_BLK_SHIFT),
				NULL
				);
			if (res != SUCCESS){
				log_errorln( "Cannot set accordance map." );
				break;
			}
		}
		if (res != SUCCESS)
			break;
	}
	return res;
}

int _snapshotdata_write_to( snapshotdata_t* snapshotdata, sparse_arr1lv_t* to_map, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	int res;
	sector_t block_inx;
	sector_t blk_pos;
	snapshotdata_blkinfo_t* blkinfo = _shapshotada_get_blkinfo( snapshotdata );
	if (blkinfo == NULL)
		return -EINVAL;

	if (snapshotdata->shared->type == SNAPSHOTDATA_TYPE_STRETCH)
		snapshotdata_stretch_check_halffill( snapshotdata->stretch, blkinfo);

	res = snapshotdata_blkinfo_pos_increment( blkinfo, blk_cnt, &blk_pos );
	if (res != SUCCESS){
		if (snapshotdata->shared->type == SNAPSHOTDATA_TYPE_STRETCH)
			snapshotdata_stretch_overflow( snapshotdata->stretch, res, blkinfo );

		log_errorln_sect( "blk_pos + size = ", (blkinfo->pos + blk_cnt) );
		log_errorln_sect( "blk_cnt = ", blkinfo->limit );
		log_errorln( "Cannot store data to snapshot. Not enough space." );
		return -ENODATA;
	}

	res = _snapshotdata_write_direct( snapshotdata, arr_ofs, arr, blk_pos, blk_cnt );
	if (res != SUCCESS){
		log_errorln( "Failed to call _snapshotdata_write_direct." );
		return res;
	}

	for (block_inx = 0; block_inx < blk_cnt; block_inx += SNAPSHOTDATA_BLK_SIZE){
		res = sparse_array_1lv_set(
			to_map,
			(blk_ofs + block_inx) >> SNAPSHOTDATA_BLK_SHIFT,
			(sparse_array_el_t)((blk_pos + block_inx) >> SNAPSHOTDATA_BLK_SHIFT),
			NULL
		);
		if (res != SUCCESS){
			log_errorln( "Cannot set accordance map." );
			break;
		}
	}

	return res;
}

void snapshotdata_SetCorrupted( snapshotdata_t* snapshotdata, int err_code )
{
	if (!snapshotdata->corrupted){
		atomic_set( &snapshotdata->corrupted_cnt, 0 );
		snapshotdata->corrupted = true;
		snapshotdata->err_code = err_code;

		log_errorln_d( "Now snapshot corrupted. error=", (0-err_code) );
	}
	else{
		log_errorln( "Snapshot already corrupted." );
	}
}

bool snapshotdata_IsCorrupted( snapshotdata_t* snapshotdata, dev_t original_dev_id )
{
	if (snapshotdata == NULL)
		return true;

	if (snapshotdata->corrupted){
		if (0 == atomic_read( &snapshotdata->corrupted_cnt )){
			log_errorln_dev_t( "Snapshot is corrupted for device ", original_dev_id );
		}
		atomic_inc( &snapshotdata->corrupted_cnt );
		return true;
	}

	return false;
}

int _snapshotdata_test_block( sparse_arr1lv_t* to_acc_map, sector_t blk_ofs, bool* p_in_snapshot )
{
	int res = SUCCESS;
	sparse_array_el_t el;

	res = sparse_array_1lv_get( to_acc_map, (blk_ofs >> SNAPSHOTDATA_BLK_SHIFT), &el );
	if (res == SUCCESS){
		*p_in_snapshot = true;
	}
	else if (res == -ENODATA){
		*p_in_snapshot = false;
		res = SUCCESS;
	}
	else{
		log_errorln_sect( "Cannot get data from snapshot map by offset=", blk_ofs );
	}

	return res;
}

int snapshotdata_get_snapofs( snapshotdata_t* snapshotdata, sector_t blk_ofs, sector_t* snap_ofs )
{
	sparse_array_el_t el;
	int res = SUCCESS;

	res = sparse_array_1lv_get( &snapshotdata->write_acc_map, (blk_ofs >> SNAPSHOTDATA_BLK_SHIFT), &el );
	if (res != SUCCESS){
		res = sparse_array_1lv_get( &snapshotdata->acc_map, (blk_ofs >> SNAPSHOTDATA_BLK_SHIFT), &el );
		if (res != SUCCESS){
			return res;
		}
	}
	*snap_ofs = ((sector_t)(el) << SNAPSHOTDATA_BLK_SHIFT);
	//log_errorln_sect( "found ", *snap_ofs );

	return res;
}

int snapshotdata_TestBlockInBothMap( snapshotdata_t* snapshotdata, sector_t blk_ofs, bool* p_in_snapshot )
{
	int res;
	sector_t snap_ofs;

	res = snapshotdata_get_snapofs( snapshotdata, blk_ofs, &snap_ofs );
	if (res == SUCCESS){
		*p_in_snapshot = true;
	}
	else{
		*p_in_snapshot = false;
		if (res == -ENODATA)
			res = SUCCESS;
	}
	return res;
}

int _snapshotdata_read_page_direct( snapshotdata_t* snapshotdata, page_info_t pg, sector_t snap_ofs )
{
	int res = SUCCESS;

	switch (snapshotdata->shared->type){
	case SNAPSHOTDATA_TYPE_STRETCH: res = snapshotdata_file_read_page_direct( &snapshotdata->stretch->file, pg, snap_ofs ); break;
	case SNAPSHOTDATA_TYPE_COMMON: res = snapshotdata_file_read_page_direct( &snapshotdata->common->file, pg, snap_ofs ); break;
	case SNAPSHOTDATA_TYPE_MEM: res = snapshotdata_buffer_read_page_direct( &snapshotdata->mem->buffer, pg, snap_ofs ); break;
	default: res = -EINVAL;
	}
	return res;
}

int _snapshotdata_read_part_direct( snapshotdata_t* snapshotdata, blk_redirect_bio_endio_t* rq_endio, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt )
{
	int res = -EINVAL;

	switch (snapshotdata->shared->type){
	case SNAPSHOTDATA_TYPE_STRETCH: res = snapshotdata_file_read_part_direct( &snapshotdata->stretch->file, rq_endio, snap_ofs, rq_ofs, blk_cnt ); break;
	case SNAPSHOTDATA_TYPE_COMMON: res = snapshotdata_file_read_part_direct( &snapshotdata->common->file, rq_endio, snap_ofs, rq_ofs, blk_cnt );  break;
	case SNAPSHOTDATA_TYPE_MEM: res = snapshotdata_buffer_read_part_direct( &snapshotdata->mem->buffer, rq_endio, snap_ofs, rq_ofs, blk_cnt ); break;
	default: res = -EINVAL;
	}
	return res;
}

int snapshotdata_read_part( snapshotdata_t* snapshotdata, blk_redirect_bio_endio_t* rq_endio, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count )
{
	int res = SUCCESS;
	sector_t buffer_ofs = 0;
	sector_t snap_ofs_start = 0;
	sector_t snap_ofs_prev = 0;
	sector_t snap_ofs_curr = 0;
	sector_t range_blk_length = 0;
	sector_t blk_ofs_inx = 0;

	sector_t unordered_ofs;
	sector_t prev_min_len = 0;

	unordered_ofs = (blk_ofs + blk_ofs_inx) & SNAPSHOTDATA_BLK_MASK;
	{
		res = snapshotdata_get_snapofs( snapshotdata, (blk_ofs + blk_ofs_inx), &snap_ofs_curr );
		if (res != SUCCESS){
			return res;
		}
		snap_ofs_curr |= unordered_ofs;

		prev_min_len = min_t( sector_t, (SNAPSHOTDATA_BLK_SIZE - unordered_ofs), (rq_count - blk_ofs_inx) );

		snap_ofs_prev = snap_ofs_curr;
		snap_ofs_start = snap_ofs_curr;
		range_blk_length = prev_min_len;

		blk_ofs_inx += prev_min_len;
	}

	for (; blk_ofs_inx < rq_count; blk_ofs_inx += SNAPSHOTDATA_BLK_SIZE){
		sector_t min_len;

		res = snapshotdata_get_snapofs( snapshotdata, (blk_ofs + blk_ofs_inx), &snap_ofs_curr );
		if (res != SUCCESS){
			return res;
		}

		min_len = min_t( sector_t, SNAPSHOTDATA_BLK_SIZE, (rq_count - blk_ofs_inx) );

		if ((snap_ofs_prev + prev_min_len) == snap_ofs_curr){
			snap_ofs_prev = snap_ofs_curr;
			range_blk_length += min_len;
			prev_min_len = min_len;
			continue;
		}

		res = _snapshotdata_read_part_direct( snapshotdata, rq_endio, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length );
		if (res != SUCCESS){
			break;
		}

		buffer_ofs += range_blk_length;

		snap_ofs_start = snap_ofs_curr;
		snap_ofs_prev = snap_ofs_curr;

		range_blk_length = min_len;

		prev_min_len = min_len;
	}
	if (res == SUCCESS){
		if (range_blk_length){
			res = _snapshotdata_read_part_direct( snapshotdata, rq_endio, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length );
		}
	}
	return res;
}

int _snapshotdata_read_dio_direct( snapshotdata_t* snapshotdata, blk_deferred_request_t* dio_req, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt, page_array_t* arr )
{
	int res = SUCCESS;
	switch (snapshotdata->shared->type)
	{
	case SNAPSHOTDATA_TYPE_STRETCH: res = snapshotdata_file_read_dio_direct_file( &snapshotdata->stretch->file, dio_req, snap_ofs, rq_ofs, blk_cnt, arr ); break;
	case SNAPSHOTDATA_TYPE_COMMON: res = snapshotdata_file_read_dio_direct_file( &snapshotdata->common->file, dio_req, snap_ofs, rq_ofs, blk_cnt, arr ); break;
	case SNAPSHOTDATA_TYPE_MEM: res = snapshotdata_buffer_read_dio_direct( &snapshotdata->mem->buffer, dio_req, snap_ofs, rq_ofs, blk_cnt, arr ); break;
	default: res = -EINVAL;
	}
	return res;
}

int snapshotdata_read_dio( snapshotdata_t* snapshotdata, blk_deferred_request_t* dio_req, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count, page_array_t* arr )
{
	int res = SUCCESS;
	sector_t buffer_ofs = 0;
	sector_t snap_ofs_start = 0;
	sector_t snap_ofs_prev = 0;
	sector_t snap_ofs_curr = 0;
	sector_t range_blk_length = 0;
	sector_t blk_ofs_inx = 0;

	sector_t unordered_ofs;
	sector_t prev_min_len = 0;

	unordered_ofs = (blk_ofs + blk_ofs_inx) & SNAPSHOTDATA_BLK_MASK;
	{
		sparse_array_el_t el;
		res = sparse_array_1lv_get( &snapshotdata->acc_map, (blk_ofs + blk_ofs_inx) >> SNAPSHOTDATA_BLK_SHIFT, &el );
		if (res != SUCCESS){
			return res;
		}
		snap_ofs_curr = ((sector_t)(el) << SNAPSHOTDATA_BLK_SHIFT) | unordered_ofs;

		prev_min_len = SNAPSHOTDATA_BLK_SIZE;

		snap_ofs_prev = snap_ofs_curr;
		snap_ofs_start = snap_ofs_curr;
		range_blk_length = prev_min_len;

		blk_ofs_inx += prev_min_len;
	}

	for (; blk_ofs_inx < rq_count; blk_ofs_inx += SNAPSHOTDATA_BLK_SIZE){
		sparse_array_el_t el;
		res = sparse_array_1lv_get( &snapshotdata->acc_map, (blk_ofs + blk_ofs_inx) >> SNAPSHOTDATA_BLK_SHIFT, &el );
		if (res != SUCCESS){
			return res;
		}

		snap_ofs_curr = ((sector_t)(el) << SNAPSHOTDATA_BLK_SHIFT);

		if ((snap_ofs_prev + prev_min_len) == snap_ofs_curr){
			snap_ofs_prev = snap_ofs_curr;
			range_blk_length += SNAPSHOTDATA_BLK_SIZE;
			prev_min_len = SNAPSHOTDATA_BLK_SIZE;
			continue;
		}

		res = _snapshotdata_read_dio_direct( snapshotdata, dio_req, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length, arr );
		if (res != SUCCESS){
			break;
		}

		buffer_ofs += range_blk_length;

		snap_ofs_start = snap_ofs_curr;
		snap_ofs_prev = snap_ofs_curr;

		range_blk_length = SNAPSHOTDATA_BLK_SIZE;

		prev_min_len = SNAPSHOTDATA_BLK_SIZE;
	}
	if (res == SUCCESS){
		if (range_blk_length){
			res = _snapshotdata_read_dio_direct( snapshotdata, dio_req, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length, arr );
		}
	}
	return res;
}

void snapshotdata_order_border( const sector_t range_start_sect, const sector_t range_cnt_sect, sector_t *out_range_start_sect, sector_t *out_range_cnt_sect )
{
	sector_t unorder_ofs;
	sector_t unorder_len;

	unorder_ofs = range_start_sect & SNAPSHOTDATA_BLK_MASK;
	*out_range_start_sect = range_start_sect - unorder_ofs;

	unorder_len = (range_cnt_sect + unorder_ofs) & SNAPSHOTDATA_BLK_MASK;
	if (unorder_len == 0)
		*out_range_cnt_sect = (range_cnt_sect + unorder_ofs);
	else
		*out_range_cnt_sect = (range_cnt_sect + unorder_ofs) + (SNAPSHOTDATA_BLK_SIZE - unorder_len);
};

int snapshotdata_write_to_image( snapshotdata_t* snapshotdata, struct bio* bio, struct block_device* defer_io_blkdev )
{
	int res = SUCCESS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif
	sector_t ord_ofs_sect;
	sector_t ord_len_sect;
	sector_t ofs_sect = bio_bi_sector( bio );
	sector_t len_sect = sector_from_size( bio_bi_size( bio ) );
	sector_t arr_ofs = 0;
	page_array_t* arr = NULL;

	//log_warnln_sect( "ofs= ", ofs_sect );
	//log_warnln_sect( "len= ", len_sect );

	snapshotdata_order_border( ofs_sect, len_sect, &ord_ofs_sect, &ord_len_sect );

	arr = page_array_alloc( page_count_calculate( ord_ofs_sect, ord_len_sect ), GFP_NOIO );
	if (arr == NULL){
		log_errorln_sect( "Cannot allocate some pages for sectors. Sectors count=", ord_len_sect );
		return -ENOMEM;
	}
	do{
		if ( ofs_sect != ord_ofs_sect ){ //preread first page needed
			sector_t first_blk = ord_ofs_sect;
			sector_t snap_ofs;

			if (SUCCESS == snapshotdata_get_snapofs( snapshotdata, first_blk, &snap_ofs )){
				res = _snapshotdata_read_page_direct( snapshotdata, arr->pg[0], snap_ofs );
			}
			else{
				res = blk_direct_submit_page( defer_io_blkdev, READ, first_blk, arr->pg[0].page );
			}
			if (res != SUCCESS){
				log_errorln_sect( "Cannot preread first block. first_blk=", first_blk );
				break;
			}

		}
		if ((ofs_sect != ord_ofs_sect) && (ord_len_sect == SNAPSHOTDATA_BLK_SIZE)){
			//first and last blocks are same block and already preread
		}else{
			if ((ord_len_sect - len_sect) > (ofs_sect - ord_ofs_sect)){ //preread last page needed
				sector_t last_blk = ord_ofs_sect + ord_len_sect - SNAPSHOTDATA_BLK_SIZE;
				sector_t snap_ofs;

				if (SUCCESS == snapshotdata_get_snapofs( snapshotdata, last_blk, &snap_ofs ))
					res = _snapshotdata_read_page_direct( snapshotdata, arr->pg[arr->pg_cnt - 1], snap_ofs );
				else
					res = blk_direct_submit_page( defer_io_blkdev, READ, last_blk, arr->pg[arr->pg_cnt - 1].page );

				if (res != SUCCESS){
					log_errorln_sect( "Cannot preread last block. last_blk=", last_blk );
					break;
				}
			}
		}
		arr_ofs = ofs_sect - ord_ofs_sect;
		bio_for_each_segment( bvec, bio, iter )
		{
			char* mem;
			unsigned int bvec_ofs;

			for (bvec_ofs = 0; bvec_ofs < bio_vec_len( bvec ); bvec_ofs += SECTOR512){
				void* dst = page_get_sector( arr, arr_ofs );

				mem = mem_kmap_atomic( bio_vec_page( bvec ) );
				memcpy( dst, mem + bio_vec_offset( bvec ) + bvec_ofs, SECTOR512 );
				mem_kunmap_atomic( mem );

				++arr_ofs;
			}
		}

		if (res == SUCCESS){
			res = _snapshotdata_write_to( snapshotdata, &snapshotdata->write_acc_map, 0, arr, ord_ofs_sect, ord_len_sect );
		}

	} while (false);
	page_array_free( arr );
	arr = NULL;

	if (res != SUCCESS){
		log_errorln_sect( "Failed. ofs=", ofs_sect );
		log_errorln_sect( "        len=", len_sect );

		log_errorln_sect( "ord_ofs_sect=", ord_ofs_sect );
		log_errorln_sect( "ord_len_sect=", ord_len_sect );
	}
	return res;
}

int _snapshotdata_check_io_compatibility( dev_t dev_id, struct block_device*  snapdata_blk_dev, bool* is_compatibility )
{
	int res = SUCCESS;
#if 0
	* is_compatibility = true;
	log_traceln_dev_t( "Compatible always for ", dev_id );
#else
	blk_dev_info_t dev_info;
	blk_dev_info_t snapdata_dev_info;

	*is_compatibility = false;

	res = blk_dev_get_info( dev_id, &dev_info );
	if (SUCCESS == res){
		res = _blk_dev_get_info( snapdata_blk_dev, &snapdata_dev_info );
		if (SUCCESS == res){

			if (snapdata_dev_info.logical_block_size <= dev_info.logical_block_size){
				log_traceln_dev_t( "Compatible snapshot data device and ", dev_id );

				//log_traceln_d( "device logical block size ", dev_info.logical_block_size );
				//log_traceln_d( "device physical block size ", dev_info.physical_block_size );
				//log_traceln_d( "device io_min ", dev_info.io_min );

				//log_traceln_d( "snapshot device logical block size ", snapdata_dev_info.logical_block_size );
				//log_traceln_d( "snapshot device physical block size ", snapdata_dev_info.physical_block_size );
				//log_traceln_d( "snapshot device io_min ", snapdata_dev_info.io_min );

				*is_compatibility = true;
			}
			else{
				log_errorln_dev_t( "Incompatible snapshot data device and ", dev_id );

				log_errorln_d( "device logical block size ", dev_info.logical_block_size );
				//log_errorln_d( "device physical block size ", dev_info.physical_block_size );
				//log_errorln_d( "device io_min ", dev_info.io_min );

				log_errorln_d( "snapshot device logical block size ", snapdata_dev_info.logical_block_size );
				//log_errorln_d( "snapshot device physical block size ", snapdata_dev_info.physical_block_size );
				//log_errorln_d( "snapshot device io_min ", snapdata_dev_info.io_min );
				*is_compatibility = false;
			}
		}
	}

	if (SUCCESS != res){
		log_errorln_dev_t( "Failed to check io compatibility for ", dev_id );
	}
#endif
	return res;

}

void snapshotdata_print_state( snapshotdata_t* snapshotdata )
{
	unsigned long wrote_mb;

	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );

	pr_warn( "sectors: copy_write=%lld \n",
		(long long int)atomic64_read( &snapshotdata->state_sectors_write ) );

	wrote_mb = (unsigned long)(atomic64_read( &snapshotdata->state_sectors_write ) >> (20 - SECTOR512_SHIFT));

	pr_warn( "bytes: copy_write=%lu MiB \n", wrote_mb );

	if (snapshotdata->corrupted){
		pr_warn( "Corrupted. Failed request count: %d MiB \n", atomic_read( &snapshotdata->corrupted_cnt ) );
	}
}
#endif // SNAPSTORE
