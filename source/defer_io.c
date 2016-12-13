#include "stdafx.h"
#include "container.h"
#include "container_spinlocking.h"
#include "queue_spinlocking.h"
#include "range.h"
#include "rangeset.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "blk_dev_utile.h"
#include "shared_resource.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"
#include "defer_io.h"
#include "veeamsnap_ioctl.h"
#include "cbt_map.h"
#include "tracker_queue.h"
#include "snapshot.h"
#include "tracker.h"

//////////////////////////////////////////////////////////////////////////

typedef struct defer_io_original_request_s{
	queue_content_sl_t content;

	sector_t sect_ofs;
	sector_t sect_len;

	struct bio* bio;
	struct request_queue *q;
	make_request_fn* make_rq_fn;
	tracker_t* pTracker;

}defer_io_original_request_t;

//////////////////////////////////////////////////////////////////////////

int __defer_io_copy_read_from_snapshot_dio( defer_io_t* defer_io, dio_request_t* dio_req, dio_t* dio )
{
	int res = -ENODATA;

	bool is_redirected = false;

	bool is_snap_prev = false;          //previous state of block
	bool is_snap_curr = false;          //current block state

	sector_t blk_ofs_start = 0;         //device range start
	sector_t blk_ofs_count = 0;         //device range length
	sector_t block_ofs_curr = 0;

	if (snapshotdata_IsCorrupted( defer_io->snapshotdata, defer_io->original_dev_id ))
		return -ENODATA;

	//enumarate state of all blocks for reading area.
	for (block_ofs_curr = 0; block_ofs_curr < dio->sect_len; block_ofs_curr += SNAPSHOTDATA_BLK_SIZE){

		res = snapshotdata_TestBlock( defer_io->snapshotdata, dio->sect_ofs + block_ofs_curr, &is_snap_curr );
		if (res != SUCCESS){
			log_errorln_sect( "TestBlock failed. pos=", dio->sect_ofs + block_ofs_curr );
			break;
		}

		if (is_snap_prev){
			if (is_snap_curr){
				blk_ofs_count += SNAPSHOTDATA_BLK_SIZE;
			}
			else{
				if (blk_ofs_count){
					//snapshot read
					res = snapshotdata_read_dio( defer_io->snapshotdata, dio_req, dio->sect_ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count, dio->buff );
					if (res != SUCCESS){
						log_errorln_d( "failed. err=", 0 - res );
						break;
					}
					is_redirected = true;
				}

				is_snap_prev = false;
				blk_ofs_start = block_ofs_curr;
				blk_ofs_count = SNAPSHOTDATA_BLK_SIZE;
			}
		}
		else{
			if (is_snap_curr){
				if (blk_ofs_count){
					//device read
					if (blk_ofs_count != dio_submit_pages( defer_io->original_blk_dev, dio_req, READ, blk_ofs_start, dio->buff, dio->sect_ofs + blk_ofs_start, blk_ofs_count )){
						log_errorln_sect( "Failed. ofs=", dio->sect_ofs + blk_ofs_start );
						res = -EIO;
						break;
					}
					is_redirected = true;
				}

				is_snap_prev = true;
				blk_ofs_start = block_ofs_curr;
				blk_ofs_count = SNAPSHOTDATA_BLK_SIZE;
			}
			else{
				//previous and current are not snapshot
				blk_ofs_count += SNAPSHOTDATA_BLK_SIZE;
			}
		}
	}

	//read last blocks range
	if ((res == SUCCESS) || (blk_ofs_count != 0)){
		if (is_snap_curr){
			res = snapshotdata_read_dio( defer_io->snapshotdata, dio_req, dio->sect_ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count, dio->buff );
		}
		else{
			if (blk_ofs_count != dio_submit_pages( defer_io->original_blk_dev, dio_req, READ, blk_ofs_start, dio->buff, dio->sect_ofs + blk_ofs_start, blk_ofs_count )){
				log_errorln_sect( "Failed. ofs=", dio->sect_ofs + blk_ofs_start );
				res = -EIO;
			}
		}
		if (res == SUCCESS)
			is_redirected = true;
		else{
			log_errorln_d( "failed. err=", 0 - res );
		}
	}

	if (res == -ENODATA){
		log_errorln( "Nothing for read." );
	}
	return res;
}

int __defer_io_copy_read_from_snapshot( defer_io_t* defer_io, dio_request_t* dio_copy_req )
{
	dio_t* dio;
	int dio_inx = 0;

	dio_request_waiting_skip( dio_copy_req );

	while (NULL != (dio = (dio_t*)dio_copy_req->dios[dio_inx])){
		int res = __defer_io_copy_read_from_snapshot_dio( defer_io, dio_copy_req, dio );
		if (res != SUCCESS)
			return res;
		++dio_inx;
	}

	return dio_request_wait( dio_copy_req );
}
//////////////////////////////////////////////////////////////////////////
int __defer_io_copy_write_to_snapshot( snapshotdata_t* snapshotdata, dio_request_t* dio_copy_req )
{
	int res;

	dio_request_waiting_skip( dio_copy_req );

	res = snapshotdata_write_dio_request_to_snapshot( snapshotdata, dio_copy_req );
	if (res != SUCCESS)
		return res;

	return dio_request_wait( dio_copy_req );
}
//////////////////////////////////////////////////////////////////////////
void __defer_io_finish( defer_io_t* defer_io, queue_sl_t* queue_in_progress )
{
	while ( !queue_sl_empty( *queue_in_progress ) ){
		bool is_write_bio;
		defer_io_original_request_t* orig_req = (defer_io_original_request_t*)queue_sl_get_first( queue_in_progress );

		is_write_bio = bio_data_dir( orig_req->bio ) && bio_has_data( orig_req->bio );

		if (orig_req->pTracker->underChangeTracking && is_write_bio){
			tracker_CbtBitmapLock( orig_req->pTracker );
			tracker_CbtBitmapSet( orig_req->pTracker, orig_req->sect_ofs, orig_req->sect_len );
		}

		orig_req->make_rq_fn( orig_req->q, orig_req->bio );

		atomic64_inc( &defer_io->state_bios_processed );
		atomic64_add( (orig_req->sect_len), &defer_io->state_sectors_processed );

		if (orig_req->pTracker->underChangeTracking && is_write_bio){
			tracker_CbtBitmapUnlock( orig_req->pTracker );
		}

		bio_put( orig_req->bio );
		queue_content_sl_free( &orig_req->content );
	}
}
//////////////////////////////////////////////////////////////////////////

void __defer_io_prepear_dios( defer_io_t* defer_io, queue_sl_t* queue_in_process, dio_request_t* dio_copy_req )
{
	int dios_count;
	sector_t dios_sectors_count;

	//first circle: extract dio from queue and create copying portion.
	dios_count = 0;
	dios_sectors_count = 0;
	while (!queue_sl_empty( defer_io->dio_queue ) && (dios_count < DEFER_IO_DIO_REQUEST_LENGTH) && (dios_sectors_count < DEFER_IO_DIO_REQUEST_SECTORS_COUNT)){

		defer_io_original_request_t* dio_orig_req = (defer_io_original_request_t*)queue_sl_get_first( &defer_io->dio_queue );
		atomic_dec( &defer_io->queue_filling_count );

		queue_sl_push_back( queue_in_process, &dio_orig_req->content );

		if (bio_data_dir( dio_orig_req->bio ) && bio_has_data( dio_orig_req->bio )){
			if (dio_copy_req){
				dio_t* copy_dio;
				sector_t ordered_start;
				sector_t ordered_len;

				snapshotdata_order_border( dio_orig_req->sect_ofs, dio_orig_req->sect_len, &ordered_start, &ordered_len );
				copy_dio = dio_alloc( ordered_start, ordered_len );
				if (copy_dio == NULL){
					log_errorln_sect( "dio alloc failed. ordered_len=", ordered_len );
				}else
					dio_request_add( dio_copy_req, copy_dio );
			}
			dios_sectors_count += dio_orig_req->sect_len;
		}
		++dios_count;
	}
}
//////////////////////////////////////////////////////////////////////////
int defer_io_work_thread( void* p )
{

	queue_sl_t queue_in_process;
	defer_io_t* defer_io = (defer_io_t*)p;

	log_traceln( "started." );

	//set_user_nice( current, -20 ); //MIN_NICE

	if (SUCCESS != queue_sl_init( &queue_in_process, sizeof( defer_io_original_request_t ), NULL )){
		log_errorln( "Failed to allocate queue_in_progress." );
		return -EFAULT;
	}

	while (!kthread_should_stop( ) || !queue_sl_empty( defer_io->dio_queue )){

		if (queue_sl_empty( defer_io->dio_queue )){
			int res = wait_event_interruptible_timeout( defer_io->queue_add_event, (!queue_sl_empty( defer_io->dio_queue )), VEEAMIMAGE_THROTTLE_TIMEOUT );
			if (-ERESTARTSYS == res){
				log_errorln( "Signal received. Event waiting complete with code=-ERESTARTSYS" );
			}
			else{
				//if (res == 0) // timeout
				//    wake_up_interruptible( &defer_io->queue_throttle_waiter );
			}
		}

		if (!queue_sl_empty( defer_io->dio_queue )){
			dio_request_t* dio_copy_req = NULL;

			if (!kthread_should_stop( ) && !snapshotdata_IsCorrupted( defer_io->snapshotdata, defer_io->original_dev_id ))
				dio_copy_req = dio_request_new( );

			__defer_io_prepear_dios( defer_io, &queue_in_process, dio_copy_req );

			if (dio_copy_req && (dio_copy_req->sect_len != 0)){
				int defer_io_result;

				defer_io_result = __defer_io_copy_read_from_snapshot( defer_io, dio_copy_req );
				if (SUCCESS == defer_io_result){
					atomic64_add( dio_copy_req->sect_len, &defer_io->state_sectors_copy_read );

					defer_io_result = __defer_io_copy_write_to_snapshot( defer_io->snapshotdata, dio_copy_req );
					if (SUCCESS != defer_io_result)
						snapshotdata_SetCorrupted( defer_io->snapshotdata, defer_io_result );
				}else
					snapshotdata_SetCorrupted( defer_io->snapshotdata, defer_io_result );
			}

			__defer_io_finish( defer_io, &queue_in_process );
			if (dio_copy_req)
				dio_request_free( dio_copy_req );
		}

		//wake up snapimage if defer io queue empty
		if (queue_sl_empty( defer_io->dio_queue )){
			wake_up_interruptible( &defer_io->queue_throttle_waiter );
		}
	}
	queue_sl_active( &defer_io->dio_queue, false );

	//waiting for all sent request complete
	__defer_io_finish( defer_io, &defer_io->dio_queue );

	if (SUCCESS != queue_sl_done( &queue_in_process)){
		log_errorln( "Failed to free queue_in_progress." );
	}

	log_traceln( "complete." );
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
void __defer_io_destroy( void* this_resource )
{
	defer_io_t* defer_io = (defer_io_t*)this_resource;

	if (NULL == defer_io)
		return;
	{
		stream_size_t processed;
		stream_size_t copyed;

		processed = atomic64_read( &defer_io->state_sectors_processed );
		copyed = atomic64_read( &defer_io->state_sectors_copy_read );

		log_traceln_lld( "Processed MiB: ", (processed >> (20-SECTOR512_SHIFT)) );
		log_traceln_lld( "Copied MiB: ", (copyed >> (20 - SECTOR512_SHIFT)) );
	}
	if (defer_io->dio_thread)
		defer_io_close( defer_io );

	queue_sl_done( &defer_io->dio_queue );

	dbg_kfree( defer_io );
	log_traceln( "complete." );
}
//////////////////////////////////////////////////////////////////////////
int defer_io_create( dev_t dev_id, struct block_device* blk_dev, defer_io_t** pp_defer_io )
{
	int res = SUCCESS;
	rangeset_t* rangset = NULL;
	defer_io_t* defer_io = NULL;
	char thread_name[32];

	log_traceln( "." );
	defer_io = dbg_kzalloc( sizeof( defer_io_t ), GFP_KERNEL );
	if (defer_io == NULL)
		return -ENOMEM;

	do{
		atomic64_set( &defer_io->state_bios_received, 0 );
		atomic64_set( &defer_io->state_bios_processed, 0 );
		atomic64_set( &defer_io->state_sectors_received, 0 );
		atomic64_set( &defer_io->state_sectors_processed, 0 );
		atomic64_set( &defer_io->state_sectors_copy_read, 0 );

		defer_io->original_dev_id = dev_id;
		defer_io->original_blk_dev = blk_dev;

		res = snapshotdata_FindByDevId( defer_io->original_dev_id, &defer_io->snapshotdata );
		if (res != SUCCESS){
			log_errorln_dev_t( "Snapshot data is not initialized for device=", dev_id );
			break;
		}
		log_traceln_dev_t( "Snapshot data using for device=", dev_id );

		init_rwsem( &defer_io->flush_lock );

		res = queue_sl_init( &defer_io->dio_queue, sizeof( defer_io_original_request_t ), NULL );

		init_waitqueue_head( &defer_io->queue_add_event );

		atomic_set( &defer_io->queue_filling_count, 0 );

		init_waitqueue_head( &defer_io->queue_throttle_waiter );

		shared_resource_init( &defer_io->sharing_header, defer_io, __defer_io_destroy );

		if (sprintf( thread_name, "%s%d:%d", "veeamdeferio", MAJOR( dev_id ), MINOR( dev_id ) ) >= DISK_NAME_LEN){
			log_errorln_dev_t( "Cannot create thread name for device ", dev_id );
			res = -EINVAL;
			break;
		}

		defer_io->dio_thread = kthread_create( defer_io_work_thread, (void *)defer_io, thread_name );
		if (IS_ERR( defer_io->dio_thread )) {
			res = PTR_ERR( defer_io->dio_thread );
			log_errorln_d( "Failed to allocate request processing thread. res=", 0 - res );
			break;
		}
		wake_up_process( defer_io->dio_thread );

	} while (false);

	if (res == SUCCESS){

		*pp_defer_io = defer_io;
		log_traceln( "complete success." );
	}
	else{
		if (rangset){
			rangeset_destroy( rangset );
			rangset = NULL;
		}
		__defer_io_destroy( defer_io );
		defer_io = NULL;
		log_errorln_d( "complete fail. res=", 0 - res );
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
int defer_io_close( defer_io_t* defer_io )
{
	int res = SUCCESS;

	log_traceln_dev_t( "for device ", defer_io->original_dev_id );
	if (defer_io->dio_thread != NULL){
		struct task_struct* dio_thread = defer_io->dio_thread;
		defer_io->dio_thread = NULL;

		res = kthread_stop( dio_thread );//stopping and waiting.
	if (res != SUCCESS){
		log_errorln_d( "Failed to stop defer_io thread. res=", 0 - res );
	}
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __defer_io_bio2dio( struct bio* bio, dio_t* dio )
{
	unsigned int copy_size;
	unsigned int copy_page_cnt;
	unsigned int page_inx;

	copy_size = bio_bi_size(bio) + bio->bi_io_vec[0].bv_offset;
	copy_page_cnt = (copy_size >> PAGE_SHIFT);
	if (copy_size & (PAGE_SIZE - 1))
		++copy_page_cnt;

	if (copy_page_cnt > dio->buff->count){
		log_errorln( "CRITICAL! copy_page_cnt > buff->count." );
		log_errorln_d( "copy_page_cnt=", copy_page_cnt );
		log_errorln_sz( "buff->count=", dio->buff->count );
		return -EFAULT;
	}

	for (page_inx = 0; page_inx < copy_page_cnt; ++page_inx){
		void* dst = dio->buff->pg[page_inx].addr;

		void* src = mem_kmap_atomic( bio->bi_io_vec[page_inx].bv_page  );
		memcpy( dst, src, PAGE_SIZE );
		mem_kunmap_atomic( src );
	}
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int defer_io_redirect_bio( defer_io_t* defer_io, struct bio *bio, sector_t sectStart, sector_t sectCount, struct request_queue *q, make_request_fn* TargetMakeRequest_fn, void* pTracker )
{
	defer_io_original_request_t* dio_orig_req;

	if (snapshotdata_IsCorrupted( defer_io->snapshotdata, defer_io->original_dev_id ))
		return -ENODATA;

	dio_orig_req = (defer_io_original_request_t*)queue_content_sl_new_opt( &defer_io->dio_queue, GFP_NOIO );
	if (dio_orig_req == NULL)
		return -ENOMEM;


	//copy data from bio to dio write buffer
	dio_orig_req->sect_ofs = sectStart;
	dio_orig_req->sect_len = sectCount;
	bio_get( dio_orig_req->bio = bio );
	dio_orig_req->q = q;
	dio_orig_req->make_rq_fn = TargetMakeRequest_fn;
	dio_orig_req->pTracker = (tracker_t*)pTracker;

	if (SUCCESS != queue_sl_push_back( &defer_io->dio_queue, &dio_orig_req->content )){
		queue_content_sl_free( &dio_orig_req->content );
		return -EFAULT;
	}

	atomic64_inc( &defer_io->state_bios_received );
	atomic64_add( sectCount, &defer_io->state_sectors_received );

	atomic_inc( &defer_io->queue_filling_count );

	wake_up_interruptible( &defer_io->queue_add_event );

	return SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
void defer_io_print_state( defer_io_t* defer_io )
{
	unsigned long received_mb;
	unsigned long processed_mb;
	unsigned long copy_read_mb;

	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );

	pr_warn( "requests in queue count=%d",
		atomic_read( &defer_io->queue_filling_count ) );

	pr_warn( "bios: received=%lld processed=%lld \n",
		(long long int)atomic64_read( &defer_io->state_bios_received ),
		(long long int)atomic64_read( &defer_io->state_bios_processed ) );

	pr_warn( "sectors: received=%lld processed=%lld copy_read=%lld\n",
		(long long int)atomic64_read( &defer_io->state_sectors_received ),
		(long long int)atomic64_read( &defer_io->state_sectors_processed ),
		(long long int)atomic64_read( &defer_io->state_sectors_copy_read ) );

	received_mb = (unsigned long)(atomic64_read( &defer_io->state_sectors_received ) >> (20 - SECTOR512_SHIFT));
	processed_mb = (unsigned long)(atomic64_read( &defer_io->state_sectors_processed ) >> (20 - SECTOR512_SHIFT));
	copy_read_mb = (unsigned long)(atomic64_read( &defer_io->state_sectors_copy_read ) >> (20 - SECTOR512_SHIFT));

	pr_warn( "bytes: received=%lu MiB processed=%lu MiB copy_read=%lu MiB\n",
		received_mb,
		processed_mb,
		copy_read_mb);

	if (defer_io->snapshotdata)
		snapshotdata_print_state( defer_io->snapshotdata );
}
//////////////////////////////////////////////////////////////////////////