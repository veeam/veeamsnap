#include "stdafx.h"

#include "mem_alloc.h"
#include "queue_spinlocking.h"
#include "container_spinlocking.h"
#include "page_array.h"
#include "blk_dev_utile.h"
#include "dio_request.h"

//////////////////////////////////////////////////////////////////////////
struct bio_set* dio_bio_set = NULL;

typedef struct dio_bio_complete_s{
	dio_request_t* dio_req;
	sector_t bio_sect_len;
}dio_bio_complete_t;

typedef struct dio_deadlocked_s
{
	content_sl_t content;
	dio_request_t* dio_req;
}dio_deadlocked_t;

static container_sl_t DioDeadlocked;

atomic64_t dio_alloc_count;
atomic64_t dio_free_count;

void dio_init( void )
{
	atomic64_set( &dio_alloc_count, 0 );
	atomic64_set( &dio_free_count, 0 );

	container_sl_init( &DioDeadlocked, sizeof( dio_deadlocked_t ), NULL );
}
void dio_done( void )
{
	content_sl_t* content;
	while (NULL != (content = container_sl_get_first( &DioDeadlocked )) )
	{
		dio_deadlocked_t* dio_locked = (dio_deadlocked_t*)content;
		if (dio_locked->dio_req->sect_len == atomic64_read( &dio_locked->dio_req->sect_processed )){
			dio_request_free( dio_locked->dio_req );
		}
		else{
			log_errorln_p( "Deadlocked dio still in memory. Address=", dio_locked->dio_req );
		}
		content_sl_free( content );
	}

	container_sl_done( &DioDeadlocked );
}

void dio_print_state( void )
{
	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );
	pr_warn( "dio allocated: %lld \n", (long long int)atomic64_read( &dio_alloc_count ) );
	pr_warn( "dio freed: %lld \n", (long long int)atomic64_read( &dio_free_count ) );
	pr_warn( "dio in use: %lld \n", (long long int)atomic64_read( &dio_alloc_count ) - (long long int)atomic64_read( &dio_free_count ) );
}
//////////////////////////////////////////////////////////////////////////
void dio_request_deadlocked( dio_request_t* dio_req )
{
	dio_deadlocked_t* dio_locked = (dio_deadlocked_t*)content_sl_new( &DioDeadlocked );
	dio_locked->dio_req = dio_req;
	container_sl_push_back( &DioDeadlocked, &dio_locked->content );

	log_warnln_p( "Deadlocked dio. Address=", dio_locked->dio_req );
}
void dio_free( dio_t* dio )
{
	if (dio->buff != NULL){
		page_array_free( dio->buff );
		dio->buff = NULL;
	}
	dbg_kfree( dio );
}
//////////////////////////////////////////////////////////////////////////
dio_t* dio_alloc( sector_t sect_ofs, sector_t sect_len )
{
	bool success = false;
	dio_t* dio = NULL;
	int page_count;

	while (NULL == (dio = dbg_kzalloc( sizeof( dio_t ), GFP_NOIO ))){
		log_errorln_sz( "Failed to allocate dio. size=", sizeof( dio_t ) );
		schedule( );
	}
	do{
		dio->sect_ofs = sect_ofs;
		dio->sect_len = sect_len;

		page_count = page_count_calculate( sect_ofs, sect_len );
		dio->buff = page_array_alloc( page_count, GFP_NOIO );
		if (dio->buff == NULL)
			break;

		success = true;
	} while (false);

	if (!success){
		log_errorln_sect( "failed. sect_len=", sect_len );
		dio_free( dio );
		dio = NULL;
	}

	return dio;
}
//////////////////////////////////////////////////////////////////////////
int dio_bioset_create( void )
{
	dio_bio_set = bioset_create( 64, sizeof( dio_bio_complete_t ) );
	if (dio_bio_set == NULL){
		log_errorln( "Failed to create bio set." );
		return -ENOMEM;
	}
	log_traceln( "Specific bio set created." );
	return SUCCESS;
}

void dio_bioset_free( void )
{
	if (dio_bio_set != NULL){
		bioset_free( dio_bio_set );
		dio_bio_set = NULL;

		log_traceln( "Specific bio set free." );
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
void dio_bio_free( struct bio* bio )
{
	bio_free( bio, dio_bio_set );
}
#endif

static inline
struct bio* dio_bio_alloc( int nr_iovecs )
{
	struct bio* new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, dio_bio_set );
	if (new_bio){
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
		new_bio->bi_destructor = dio_bio_free;
#endif
		new_bio->bi_private = ((void*)new_bio) - sizeof( dio_bio_complete_t );
	}
	return new_bio;
}

//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int dio_bio_end_io( struct bio *bio, unsigned int size, int err )
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void dio_bio_end_io( struct bio *bio, int err )
#else
void dio_bio_end_io( struct bio *bio )
#endif
{
	int local_err;
	dio_bio_complete_t* complete_param = (dio_bio_complete_t*)bio->bi_private;

	if (complete_param == NULL){
		WARN( true, "bio already end." );
	}else{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
		local_err = err;
#else
		local_err = bio->bi_error;
#endif
		__dio_bio_end_io( complete_param->dio_req, complete_param->bio_sect_len, local_err );
		bio->bi_private = NULL;
	}

	bio_put( bio );

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	return err;
#endif
}
//////////////////////////////////////////////////////////////////////////
sector_t __dio_submit_pages(
struct block_device* blk_dev,
	dio_request_t* dio_req,
	int direction,
	sector_t arr_ofs,
	page_array_t* arr,
	sector_t ofs_sector,
	sector_t size_sector
){

	struct bio *bio = NULL;
	int nr_iovecs;
	int page_inx = arr_ofs >> (PAGE_SHIFT - SECTOR512_SHIFT);
	sector_t process_sect = 0;

	nr_iovecs = page_count_calculate( ofs_sector, size_sector );

	while (NULL == (bio = dio_bio_alloc( nr_iovecs ))){
		log_traceln_d( "dio_bio_alloc failed. nr_iovecs=", nr_iovecs );

		size_sector = (size_sector >> 1) & ~(SECTORS_IN_PAGE - 1);
		if (size_sector == 0){
			return 0;
		}
		nr_iovecs = page_count_calculate( ofs_sector, size_sector );
	}

	bio->bi_bdev = blk_dev;
	bio->bi_end_io = dio_bio_end_io;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	bio->bi_rw = direction;
#else
	if (direction == READ)
		bio_set_op_attrs( bio, REQ_OP_READ, 0 );
	else
		bio_set_op_attrs( bio, REQ_OP_WRITE, 0 );
#endif
	bio_bi_sector( bio ) = ofs_sector;

	{//add first
		sector_t unordered = arr_ofs & (SECTORS_IN_PAGE - 1);
		sector_t bvec_len_sect = min_t( sector_t, (SECTORS_IN_PAGE - unordered), size_sector );

		WARN( (page_inx > arr->count), "Invalid page_array index." );
		if (0 == bio_add_page( bio, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), sector_to_uint( unordered ) )){
			log_errorln_d( "bvec full! bi_size=", bio_bi_size( bio ) );
			bio_put( bio );
			return 0;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}

	while ((process_sect < size_sector) && (page_inx < arr->count))
	{
		sector_t bvec_len_sect = min_t( sector_t, SECTORS_IN_PAGE, (size_sector - process_sect) );

		WARN( (page_inx > arr->count), "Invalid page_array index." );
		if (0 == bio_add_page( bio, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), 0 )){
			break;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}


	((dio_bio_complete_t*)bio->bi_private)->dio_req = dio_req;
	((dio_bio_complete_t*)bio->bi_private)->bio_sect_len = process_sect;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	submit_bio( direction, bio );
#else
	submit_bio( bio );
#endif

	return process_sect;
}
//////////////////////////////////////////////////////////////////////////
sector_t dio_submit_pages(
    struct block_device* blk_dev,
	dio_request_t* dio_req,
	int direction,
	sector_t arr_ofs,
	page_array_t* arr,
	sector_t ofs_sector,
	sector_t size_sector
){
	sector_t process_sect = 0;
	do{
		sector_t portion_sect = __dio_submit_pages( blk_dev, dio_req, direction, arr_ofs + process_sect, arr, ofs_sector + process_sect, size_sector - process_sect );
		if (portion_sect == 0){
			log_errorln_sect( "Failed. Processed only=", process_sect );
			break;
		}
		process_sect += portion_sect;
	} while (process_sect < size_sector);

	return process_sect;
}
//////////////////////////////////////////////////////////////////////////
void dio_memcpy_read( char* databuff, dio_request_t* dio_req, page_array_t* arr, sector_t arr_ofs, sector_t size_sector )
{
	sector_t sect_inx;
	for (sect_inx = 0; sect_inx < size_sector; ++sect_inx){
		memcpy( page_get_sector( arr, sect_inx + arr_ofs ), databuff + (sect_inx<<SECTOR512_SHIFT), SECTOR512 );
	}
	__dio_bio_end_io( dio_req, size_sector, SUCCESS );
}

dio_request_t* dio_request_new( void )
{
	dio_request_t* dio_req = NULL;

	while (NULL == (dio_req = dbg_kzalloc( sizeof( dio_request_t ), GFP_NOIO ))){
		log_errorln_sz( "Failed to allocate dio_request. size=", sizeof( dio_request_t ) );
	}
	dio_req->dios_cnt = 0;
	dio_req->result = SUCCESS;
	atomic64_set( &dio_req->sect_processed, 0 );
	dio_req->sect_len = 0;
	init_completion( &dio_req->complete );

	return dio_req;
}

bool dio_request_add( dio_request_t* dio_req, dio_t* dio )
{
	if (dio_req->dios_cnt < DEFER_IO_DIO_REQUEST_LENGTH){
		dio_req->dios[dio_req->dios_cnt] = dio;
		++dio_req->dios_cnt;

		dio_req->sect_len += dio->sect_len;
		return true;
	}
	return false;
}

void dio_request_free( dio_request_t* dio_req )
{
	if (dio_req != NULL){
		int inx = 0;

		for (inx = 0; inx < dio_req->dios_cnt; ++inx){
			if (dio_req->dios[inx]){
				dio_free( dio_req->dios[inx] );
				dio_req->dios[inx] = NULL;
			}
		}
		dbg_kfree( dio_req );
	}
}

void dio_request_waiting_skip( dio_request_t* dio_req )
{
	init_completion( &dio_req->complete );
	atomic64_set( &dio_req->sect_processed, 0 );
}

int dio_request_wait( dio_request_t* dio_req )
{
	u64 start_jiffies = get_jiffies_64( );
	u64 current_jiffies;
	//wait_for_completion_io_timeout

	//if (0 == wait_for_completion_timeout( &dio_req->complete, (HZ * 30) )){
	while (0 == wait_for_completion_timeout( &dio_req->complete, (HZ * 1) )){
		//log_warnln( "differed IO request timeout" );
		//log_errorln_sect( "sect_len=", dio_req->sect_len );
		//log_errorln_sect( "sect_processed=", atomic64_read( &dio_req->sect_processed ) );
		//WARN( true, "%s timeout.", __FUNCTION__ );
		//return -EFAULT;

		current_jiffies = get_jiffies_64( );
		if (jiffies_to_msecs( current_jiffies - start_jiffies ) > 30 * 1000){
			log_warnln( "differed IO request timeout" );
			//log_errorln_sect( "sect_processed=", atomic64_read( &dio_req->sect_processed ) );
			//log_errorln_sect( "sect_len=", dio_req->sect_len );
			return -EDEADLK;
		}
	}
	return dio_req->result;
}

//////////////////////////////////////////////////////////////////////////

