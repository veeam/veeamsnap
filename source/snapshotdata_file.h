#pragma once

#ifndef SNAPSTORE

#include "page_array.h"
#include "rangevector.h"
#include "blk_deferred.h"
#include "blk_redirect.h"


typedef struct snapshotdata_file_s{
	dev_t blk_dev_id;
	struct block_device*  blk_dev;
	rangevector_t dataranges;

}snapshotdata_file_t;

int snapshotdata_file_create( snapshotdata_file_t* file, dev_t dev_id );
void snapshotdata_file_destroy( snapshotdata_file_t* file );

int snapshotdata_file_read_page_direct( snapshotdata_file_t* file, page_info_t pg, sector_t snap_ofs );
int snapshotdata_file_read_part_direct( snapshotdata_file_t* file, blk_redirect_bio_endio_t* rq_endio, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt );
int snapshotdata_file_read_dio_direct_file( snapshotdata_file_t* file, blk_deferred_request_t* dio_req, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt, page_array_t* arr );

int snapshotdata_file_direct_write( snapshotdata_file_t* file, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt );
int snapshotdata_file_direct_write_dio( snapshotdata_file_t* file, blk_deferred_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt );

#endif //SNAPSTORE
