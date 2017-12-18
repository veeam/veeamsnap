#pragma once

#ifndef SNAPSTORE

#include "page_array.h"
#include "blk_redirect.h"
#include "blk_deferred.h"


typedef struct snapshotdata_buffer_s{
	void*  buff;
	size_t buff_size;
}snapshotdata_buffer_t;

int snapshotdata_buffer_create( snapshotdata_buffer_t* buffer, size_t buffer_size );

void snapshotdata_buffer_destroy( snapshotdata_buffer_t* buffer );

int snapshotdata_buffer_read_page_direct( snapshotdata_buffer_t* buffer, page_info_t pg, sector_t snap_ofs );
int snapshotdata_buffer_read_part_direct( snapshotdata_buffer_t* buffer, blk_redirect_bio_endio_t* rq_endio, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt );
int snapshotdata_buffer_read_dio_direct( snapshotdata_buffer_t* buffer, blk_deferred_request_t* dio_req, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt, page_array_t* arr );

int snapshotdata_buffer_write_direct( snapshotdata_buffer_t* buffer, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt );
int snapshotdata_buffer_write_dio_direct( snapshotdata_buffer_t* buffer, blk_deferred_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt );

#endif //SNAPSTORE
