#include "stdafx.h"
#include "snapshotdata_file.h"

#ifndef SNAPSTORE

#include "blk_direct.h"


int snapshotdata_file_create( snapshotdata_file_t* file, dev_t dev_id )
{
	int res = blk_dev_open( dev_id, &file->blk_dev );
	if (res != SUCCESS){
		log_errorln_d( "Failed to open snapshot device res=", res );
		return res;
	}

	rangevector_init( &file->dataranges, true );
	file->blk_dev_id = dev_id;

	return SUCCESS;
}

void snapshotdata_file_destroy( snapshotdata_file_t* file )
{
	rangevector_done( &file->dataranges );
	if (file->blk_dev != NULL){
		blk_dev_close( file->blk_dev );
		file->blk_dev = NULL;
	}
}

int snapshotdata_file_read_page_direct( snapshotdata_file_t* file, page_info_t pg, sector_t snap_ofs )
{
	sector_t real_ofs;
	sector_t real_size;
	int res = rangevector_v2p( &file->dataranges, snap_ofs, (PAGE_SIZE / SECTOR512), &real_ofs, &real_size );
	if (res == SUCCESS){
		res = blk_direct_submit_page( file->blk_dev, READ, real_ofs, pg.page );
		if (res != SUCCESS){
			log_errorln_sect( "Failed to read data from snapshot. real_ofs=", real_ofs );
		}
	}
	else{
		log_errorln( "Cannot do range conversion." );
	}
	return res;
}

int snapshotdata_file_read_part_direct( snapshotdata_file_t* file, blk_redirect_bio_endio_t* rq_endio, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt )
{
	int res = SUCCESS;
	sector_t ofs = 0;
	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		res = rangevector_v2p( &file->dataranges, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln( "Cannot do range conversion." );
			break;
		}
		res = blk_dev_redirect_part( rq_endio, READ, file->blk_dev, real_ofs, rq_ofs + ofs, real_size );
		if (res != SUCCESS){
			log_errorln( "Failed to read data from snapshot." );
			break;
		}
		ofs += real_size;
	}
	return res;
}

int snapshotdata_file_read_dio_direct_file( snapshotdata_file_t* file, blk_deferred_request_t* dio_req, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt, page_array_t* arr )
{
	int res = SUCCESS;
	sector_t ofs = 0;
	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		res = rangevector_v2p( &file->dataranges, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln( "Cannot do range conversion." );
			break;
		}

		if (real_size != blk_deferred_submit_pages( file->blk_dev, dio_req, READ, rq_ofs + ofs, arr, real_ofs, real_size )){
			log_errorln( "Failed to read data from snapshot." );
			res = -EFAULT;
			break;
		}
		ofs += real_size;
	}
	return res;
}


int snapshotdata_file_direct_write( snapshotdata_file_t* file, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	sector_t ofs = 0;
	int res = SUCCESS;

	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangevector_v2p( &file->dataranges, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length. error code=", res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = blk_direct_submit_pages(
			file->blk_dev,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);

		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot. res=", res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}
	return res;
}

int snapshotdata_file_direct_write_dio( snapshotdata_file_t* file, blk_deferred_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	sector_t ofs = 0;
	int res = SUCCESS;

	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangevector_v2p( &file->dataranges, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length. res=", res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = blk_deferred_submit_pages(
			file->blk_dev,
			dio_req,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);

		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot. res=", res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}

	return res;
}

#endif //SNAPSTORE
