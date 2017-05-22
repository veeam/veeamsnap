#pragma once

#ifndef DIO_REQUEST_H
#define DIO_REQUEST_H

typedef struct dio_s
{
	sector_t sect_ofs;
	sector_t sect_len;

	page_array_t* buff;
}dio_t;

typedef struct dio_request_s
{
	struct completion complete;
	sector_t sect_len;
	atomic64_t sect_processed;
	int result;
	int dios_cnt;
	dio_t* dios[DEFER_IO_DIO_REQUEST_LENGTH];
}dio_request_t;

//////////////////////////////////////////////////////////////////////////
void dio_init( void );
void dio_done( void );
void dio_print_state( void );

dio_t* dio_alloc( sector_t sect_ofs, sector_t sect_len );
void dio_free( dio_t* dio );

static inline void __dio_bio_end_io( dio_request_t* dio_req, sector_t portion_sect_cnt, int result )
{
	atomic64_add( portion_sect_cnt, &dio_req->sect_processed );

	if (dio_req->sect_len == atomic64_read( &dio_req->sect_processed )){
		complete( &dio_req->complete );
	}

	if (result != SUCCESS)
		dio_req->result = result;

}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int dio_bio_end_io( struct bio *bio, unsigned int size, int err );
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void dio_bio_end_io( struct bio *bio, int err );
#else
void dio_bio_end_io( struct bio *bio );
#endif

sector_t dio_submit_pages( struct block_device* blk_dev, dio_request_t* dio_req, int direction, sector_t arr_ofs, page_array_t* arr, sector_t ofs_sector, sector_t size_sector );

void dio_memcpy_read( char* databuff, dio_request_t* dio_req, page_array_t* arr, sector_t arr_ofs, sector_t size_sector );

//////////////////////////////////////////////////////////////////////////
dio_request_t* dio_request_new( void );

bool dio_request_add( dio_request_t* dio_req, dio_t* dio );
void dio_request_free( dio_request_t* dio_req );
void dio_request_deadlocked( dio_request_t* dio_req );

void dio_request_waiting_skip( dio_request_t* dio_req );
int dio_request_wait( dio_request_t* dio_req );

int dio_bioset_create( void );
void dio_bioset_free( void );

//////////////////////////////////////////////////////////////////////////

#endif //DIO_REQUEST_H
