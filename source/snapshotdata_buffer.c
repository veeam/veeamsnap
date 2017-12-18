#include "stdafx.h"

#ifndef SNAPSTORE

#include "snapshotdata_buffer.h"


int snapshotdata_buffer_create( snapshotdata_buffer_t* buffer, size_t buffer_size )
{
	buffer->buff_size = buffer_size;
	buffer->buff = dbg_vmalloc( buffer->buff_size );
	if (buffer->buff == NULL)
		return -ENOMEM;

	log_traceln_sz( "Memory size allocated=", buffer->buff_size );
	return SUCCESS;
}

void snapshotdata_buffer_destroy( snapshotdata_buffer_t* buffer )
{
	if (buffer->buff != NULL){
		dbg_vfree( buffer->buff, buffer->buff_size );
		buffer->buff = NULL;
	}
}

#define BUFFER_OVERFLOW_CHECK(_blk_ofs,_blk_cnt,_buff_size ) \
if ((sector_to_size( _blk_ofs ) + sector_to_size( _blk_cnt )) > _buff_size){ \
    log_errorln( "Buffer overflow" ); \
    log_errorln_sz( "buffer size = ", _buff_size ); \
    log_errorln_sz( "offset = ", sector_to_size( _blk_ofs ) ); \
    log_errorln_sz( "length = ", sector_to_size( _blk_cnt ) ); \
    return -EINVAL; \
}

int snapshotdata_buffer_read_page_direct( snapshotdata_buffer_t* buffer, page_info_t pg, sector_t snap_ofs )
{
	BUFFER_OVERFLOW_CHECK( snap_ofs, (sector_t)(PAGE_SIZE/SECTOR512), buffer->buff_size )

	memcpy( pg.addr, (char*)(buffer->buff) + sector_to_size( snap_ofs ), PAGE_SIZE );
	return SUCCESS;
}


int snapshotdata_buffer_read_part_direct( snapshotdata_buffer_t* buffer, blk_redirect_bio_endio_t* rq_endio, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt )
{
	BUFFER_OVERFLOW_CHECK( snap_ofs, blk_cnt, buffer->buff_size )

	blk_dev_redirect_memcpy_part(
		rq_endio,
		READ,
		buffer->buff + sector_to_size( snap_ofs ),
		rq_ofs,
		blk_cnt
		);
	return SUCCESS;
}

int snapshotdata_buffer_read_dio_direct( snapshotdata_buffer_t* buffer, blk_deferred_request_t* dio_req, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt, page_array_t* arr )
{
	BUFFER_OVERFLOW_CHECK( snap_ofs, blk_cnt, buffer->buff_size )
	blk_deferred_memcpy_read( buffer->buff + sector_to_size( snap_ofs ), dio_req, arr, rq_ofs, blk_cnt );
	return SUCCESS;
}


int snapshotdata_buffer_write_direct( snapshotdata_buffer_t* buffer, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	BUFFER_OVERFLOW_CHECK( blk_ofs, blk_cnt, buffer->buff_size )

	page_array_pages2mem(
		(buffer->buff) + sector_to_size( blk_ofs ),
		sector_to_size( arr_ofs ),
		arr,
		sector_to_size( blk_cnt )
		);
	return SUCCESS;
}

int snapshotdata_buffer_write_dio_direct( snapshotdata_buffer_t* buffer, blk_deferred_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	BUFFER_OVERFLOW_CHECK( blk_ofs, blk_cnt, buffer->buff_size )

	page_array_pages2mem(
		(buffer->buff) + sector_to_size( blk_ofs ),
		sector_to_size( arr_ofs ),
		arr,
		sector_to_size( blk_cnt )
	);
	blk_deferred_complete( dio_req, blk_cnt, SUCCESS );

	return SUCCESS;
}

#endif //SNAPSTORE
