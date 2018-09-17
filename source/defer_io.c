#include "stdafx.h"
#include "defer_io.h"
#include "queue_spinlocking.h"
#include "blk_deferred.h"
#include "tracker.h"
#include "blk_util.h"


typedef struct defer_io_original_request_s{
	queue_content_sl_t content;

	range_t sect;

	struct bio* bio;
	struct request_queue *q;
	make_request_fn* make_rq_fn;
	tracker_t* pTracker;

}defer_io_original_request_t;

#ifdef SNAPSTORE
//
#else //SNAPSTORE

int _defer_io_copy_read_from_snapshot_dio( defer_io_t* defer_io, blk_deferred_request_t* dio_req, blk_deferred_t* dio )
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
	for (block_ofs_curr = 0; block_ofs_curr < dio->sect.cnt; block_ofs_curr += SNAPSHOTDATA_BLK_SIZE){

		res = snapshotdata_TestBlock( defer_io->snapshotdata, dio->sect.ofs + block_ofs_curr, &is_snap_curr );
		if (res != SUCCESS){
			log_errorln_sect( "TestBlock failed. pos=", dio->sect.ofs + block_ofs_curr );
			break;
		}

		if (is_snap_prev){
			if (is_snap_curr){
				blk_ofs_count += SNAPSHOTDATA_BLK_SIZE;
			}
			else{
				if (blk_ofs_count){
					//snapshot read
					res = snapshotdata_read_dio( defer_io->snapshotdata, dio_req, dio->sect.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count, dio->buff );
					if (res != SUCCESS){
						log_errorln_d( "failed. err=", res );
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
					if (blk_ofs_count != blk_deferred_submit_pages( defer_io->original_blk_dev, dio_req, READ, blk_ofs_start, dio->buff, dio->sect.ofs + blk_ofs_start, blk_ofs_count )){
						log_errorln_sect( "Failed. ofs=", dio->sect.ofs + blk_ofs_start );
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
			res = snapshotdata_read_dio( defer_io->snapshotdata, dio_req, dio->sect.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count, dio->buff );
		}
		else{
			if (blk_ofs_count != blk_deferred_submit_pages( defer_io->original_blk_dev, dio_req, READ, blk_ofs_start, dio->buff, dio->sect.ofs + blk_ofs_start, blk_ofs_count )){
				log_errorln_sect( "Failed. ofs=", dio->sect.ofs + blk_ofs_start );
				res = -EIO;
			}
		}
		if (res == SUCCESS)
			is_redirected = true;
		else{
			log_errorln_d( "failed. err=", res );
		}
	}

	if (res == -ENODATA){
		log_errorln( "Nothing for read." );
	}
	return res;
}
#endif //SNAPSTORE

#ifdef SNAPSTORE

#else //SNAPSTORE

int _defer_io_copy_read_from_snapshot( defer_io_t* defer_io, blk_deferred_request_t* dio_copy_req )
{
	blk_deferred_t* dio;
	int dio_inx = 0;

	blk_deferred_request_waiting_skip( dio_copy_req );

	while (NULL != (dio = (blk_deferred_t*)dio_copy_req->dios[dio_inx])){
		int res = _defer_io_copy_read_from_snapshot_dio( defer_io, dio_copy_req, dio );
		if (res != SUCCESS)
			return res;
		++dio_inx;
	}

	return blk_deferred_request_wait( dio_copy_req );
}
#endif //SNAPSTORE

#ifdef SNAPSTORE
//
#else //SNAPSTORE

int _defer_io_copy_write_to_snapshot( snapshotdata_t* snapshotdata, blk_deferred_request_t* dio_copy_req )
{
	int res;

	blk_deferred_request_waiting_skip( dio_copy_req );

	res = snapshotdata_write_dio_request_to_snapshot( snapshotdata, dio_copy_req );
	if (res != SUCCESS)
		return res;

	return blk_deferred_request_wait( dio_copy_req );
}

#endif //SNAPSTORE

void _defer_io_finish( defer_io_t* defer_io, queue_sl_t* queue_in_progress )
{
	while ( !queue_sl_empty( *queue_in_progress ) )
	{
		tracker_t* pTracker = NULL;
		bool cbt_locked = false;
		bool is_write_bio;
		defer_io_original_request_t* orig_req = (defer_io_original_request_t*)queue_sl_get_first( queue_in_progress );

		is_write_bio = bio_data_dir( orig_req->bio ) && bio_has_data( orig_req->bio );

		if (orig_req->pTracker->underChangeTracking && is_write_bio){
			pTracker = orig_req->pTracker;
			cbt_locked = tracker_CbtBitmapLock( pTracker );
			if (cbt_locked)
				tracker_CbtBitmapSet( pTracker, orig_req->sect.ofs, orig_req->sect.cnt );
		}

		{
			struct bio* _bio = orig_req->bio;
			orig_req->bio = NULL;
			bio_put( _bio );

			orig_req->make_rq_fn( orig_req->q, _bio );

		}
		atomic64_inc( &defer_io->state_bios_processed );
		atomic64_add( (orig_req->sect.cnt), &defer_io->state_sectors_processed );

		if (cbt_locked)
			tracker_CbtBitmapUnlock( pTracker );

		queue_content_sl_free( &orig_req->content );
	}
}

#ifdef SNAPSTORE

int _defer_io_copy_prepare( defer_io_t* defer_io, queue_sl_t* queue_in_process, blk_deferred_request_t** dio_copy_req )
{
	int res = SUCCESS;
	int dios_count = 0;
	sector_t dios_sectors_count = 0;

	//fill copy_request set
	while (!queue_sl_empty( defer_io->dio_queue ) && (dios_count < DEFER_IO_DIO_REQUEST_LENGTH) && (dios_sectors_count < DEFER_IO_DIO_REQUEST_SECTORS_COUNT)){

		defer_io_original_request_t* dio_orig_req = (defer_io_original_request_t*)queue_sl_get_first( &defer_io->dio_queue );
		atomic_dec( &defer_io->queue_filling_count );

		queue_sl_push_back( queue_in_process, &dio_orig_req->content );

		if (!kthread_should_stop( ) && !snapstore_device_is_corrupted( defer_io->snapstore_device )){
			if (bio_data_dir( dio_orig_req->bio ) && bio_has_data( dio_orig_req->bio )){
				res = snapstore_device_prepare_requests( defer_io->snapstore_device, &dio_orig_req->sect, dio_copy_req );
				if (res != SUCCESS){
					log_errorln_d( "Failed to add range for COW. error=", res );
					break;
				}
				dios_sectors_count += dio_orig_req->sect.cnt;
			}
		}
		++dios_count;
	}
	return res;
}

#else //no SNAPSTORE

void _defer_io_prepare_dios( defer_io_t* defer_io, queue_sl_t* queue_in_process, blk_deferred_request_t* dio_copy_req )
{
	int dios_count = 0;
	sector_t dios_sectors_count = 0;

	//first circle: extract dio from queue and create copying portion.
	while (!queue_sl_empty( defer_io->dio_queue ) && (dios_count < DEFER_IO_DIO_REQUEST_LENGTH) && (dios_sectors_count < DEFER_IO_DIO_REQUEST_SECTORS_COUNT)){

		defer_io_original_request_t* dio_orig_req = (defer_io_original_request_t*)queue_sl_get_first( &defer_io->dio_queue );
		atomic_dec( &defer_io->queue_filling_count );

		queue_sl_push_back( queue_in_process, &dio_orig_req->content );

		if (bio_data_dir( dio_orig_req->bio ) && bio_has_data( dio_orig_req->bio )){
			if (dio_copy_req){

				blk_deferred_t* copy_dio;
				sector_t ordered_start;
				sector_t ordered_len;

				snapshotdata_order_border( dio_orig_req->sect.ofs, dio_orig_req->sect.cnt, &ordered_start, &ordered_len );
				copy_dio = blk_deferred_alloc( ordered_start, ordered_len );
				if (copy_dio == NULL){
					log_errorln_sect( "dio alloc failed. ordered_len=", ordered_len );
				}
				else{
					/*int res = */
					blk_deferred_request_add( dio_copy_req, copy_dio );
				}
			}
			dios_sectors_count += dio_orig_req->sect.cnt;
		}
		++dios_count;
	}
}

#endif // no SNAPSTORE

int defer_io_work_thread( void* p )
{

	queue_sl_t queue_in_process;
	defer_io_t* defer_io = (defer_io_t*)p;

	log_traceln( "started." );

	//set_user_nice( current, -20 ); //MIN_NICE

	if (SUCCESS != queue_sl_init( &queue_in_process, sizeof( defer_io_original_request_t ) )){
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
#ifdef SNAPSTORE
		if (!queue_sl_empty( defer_io->dio_queue )){
			int dio_copy_result = SUCCESS;
			blk_deferred_request_t* dio_copy_req = NULL;


			_snapstore_device_descr_read_lock( defer_io->snapstore_device );
			do{
				dio_copy_result = _defer_io_copy_prepare( defer_io, &queue_in_process, &dio_copy_req );
				if (dio_copy_result != SUCCESS){
					log_errorln_d( "Failed to prepare copy requests ", dio_copy_result );
					break;
				}
				if (NULL == dio_copy_req)
					break;//nothing to copy

				dio_copy_result = blk_deferred_request_read_original( defer_io->original_blk_dev, dio_copy_req );
				if (dio_copy_result != SUCCESS){
					log_errorln_d( "Failed to read data for COW. err=", dio_copy_result );
					break;
				}
				dio_copy_result = snapstore_device_store( defer_io->snapstore_device, dio_copy_req );
				if (dio_copy_result != SUCCESS){
					log_errorln_d( "Failed to write data for COW. err=", dio_copy_result );
					break;
				}

				atomic64_add( dio_copy_req->sect_len, &defer_io->state_sectors_copy_read );
			} while (false);

			_defer_io_finish( defer_io, &queue_in_process );

			_snapstore_device_descr_read_unlock( defer_io->snapstore_device );

			if (dio_copy_req){
				if (dio_copy_result == -EDEADLK)
					blk_deferred_request_deadlocked( dio_copy_req );
				else
					blk_deferred_request_free( dio_copy_req );
			}
		}
#else //SNAPSTORE
		if (!queue_sl_empty( defer_io->dio_queue )){
			int dio_copy_result = SUCCESS;
			blk_deferred_request_t* dio_copy_req = NULL;

			if (!kthread_should_stop( ) && !snapshotdata_IsCorrupted( defer_io->snapshotdata, defer_io->original_dev_id ))
				dio_copy_req = blk_deferred_request_new( );

			_defer_io_prepare_dios( defer_io, &queue_in_process, dio_copy_req );

			if (dio_copy_req && (dio_copy_req->sect_len != 0)){

				dio_copy_result = _defer_io_copy_read_from_snapshot( defer_io, dio_copy_req );
				if (SUCCESS == dio_copy_result){
					atomic64_add( dio_copy_req->sect_len, &defer_io->state_sectors_copy_read );

					dio_copy_result = _defer_io_copy_write_to_snapshot( defer_io->snapshotdata, dio_copy_req );
					if (SUCCESS != dio_copy_result)
						snapshotdata_SetCorrupted( defer_io->snapshotdata, dio_copy_result );
				}else
					snapshotdata_SetCorrupted( defer_io->snapshotdata, dio_copy_result );
			}

			_defer_io_finish( defer_io, &queue_in_process );
			if (dio_copy_req){
				if (dio_copy_result == -EDEADLK)
					blk_deferred_request_deadlocked( dio_copy_req );
				else
					blk_deferred_request_free( dio_copy_req );
			}
		}
#endif //SNAPSTORE

		//wake up snapimage if defer io queue empty
		if (queue_sl_empty( defer_io->dio_queue )){
			wake_up_interruptible( &defer_io->queue_throttle_waiter );
		}
	}
	queue_sl_active( &defer_io->dio_queue, false );

	//waiting for all sent request complete
	_defer_io_finish( defer_io, &defer_io->dio_queue );

	if (SUCCESS != queue_sl_done( &queue_in_process)){
		log_errorln( "Failed to free queue_in_progress." );
	}

	log_traceln( "complete." );
	return SUCCESS;
}


void _defer_io_destroy( void* this_resource )
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
		defer_io_destroy( defer_io );

	queue_sl_done( &defer_io->dio_queue );

#ifdef SNAPSTORE
	snapstore_device_put_resource( defer_io->snapstore_device );
#endif
	dbg_kfree( defer_io );
	log_traceln( "complete." );
}


int defer_io_create( dev_t dev_id, struct block_device* blk_dev, defer_io_t** pp_defer_io )
{
	int res = SUCCESS;
	defer_io_t* defer_io = NULL;
	char thread_name[32];

	log_traceln_dev_t( "device=", dev_id );

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
#ifdef SNAPSTORE
		{
			snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( defer_io->original_dev_id );
			if (NULL == snapstore_device){
				log_errorln_dev_t( "Snapshot data is not initialized for device=", dev_id );
				break;
			}
			defer_io->snapstore_device = snapstore_device_get_resource( snapstore_device );
		}
#else
		res = snapshotdata_FindByDevId( defer_io->original_dev_id, &defer_io->snapshotdata );
		if (res != SUCCESS){
			log_errorln_dev_t( "Snapshot data is not initialized for device=", dev_id );
			break;
		}
#endif

		init_rwsem( &defer_io->flush_lock );

		res = queue_sl_init( &defer_io->dio_queue, sizeof( defer_io_original_request_t ) );

		init_waitqueue_head( &defer_io->queue_add_event );

		atomic_set( &defer_io->queue_filling_count, 0 );

		init_waitqueue_head( &defer_io->queue_throttle_waiter );

		shared_resource_init( &defer_io->sharing_header, defer_io, _defer_io_destroy );

		if (sprintf( thread_name, "%s%d:%d", "veeamdeferio", MAJOR( dev_id ), MINOR( dev_id ) ) >= DISK_NAME_LEN){
			log_errorln_dev_t( "Cannot create thread name for device ", dev_id );
			res = -EINVAL;
			break;
		}

		defer_io->dio_thread = kthread_create( defer_io_work_thread, (void *)defer_io, thread_name );
		if (IS_ERR( defer_io->dio_thread )) {
			res = PTR_ERR( defer_io->dio_thread );
			log_errorln_d( "Failed to allocate request processing thread. res=", res );
			break;
		}
		wake_up_process( defer_io->dio_thread );

	} while (false);

	if (res == SUCCESS){

		*pp_defer_io = defer_io;
		log_traceln( "complete" );
	}
	else{
		_defer_io_destroy( defer_io );
		defer_io = NULL;
		log_errorln_d( "complete fail. res=", res );
	}

	return res;
}


int defer_io_destroy( defer_io_t* defer_io )
{
	int res = SUCCESS;

	log_traceln_dev_t( "for device ", defer_io->original_dev_id );
	if (defer_io->dio_thread != NULL){
		struct task_struct* dio_thread = defer_io->dio_thread;
		defer_io->dio_thread = NULL;

		res = kthread_stop( dio_thread );//stopping and waiting.
	if (res != SUCCESS){
		log_errorln_d( "Failed to stop defer_io thread. res=", res );
	}
	}
	return res;
}


int _defer_io_bio2dio( struct bio* bio, blk_deferred_t* dio )
{
	unsigned int copy_size;
	unsigned int copy_page_cnt;
	unsigned int page_inx;

	copy_size = bio_bi_size(bio) + bio->bi_io_vec[0].bv_offset;
	copy_page_cnt = (copy_size >> PAGE_SHIFT);
	if (copy_size & (PAGE_SIZE - 1))
		++copy_page_cnt;

	if (copy_page_cnt > dio->buff->pg_cnt){
		log_errorln( "CRITICAL! copy_page_cnt > buff->count." );
		log_errorln_d( "copy_page_cnt=", copy_page_cnt );
		log_errorln_sz( "buff->count=", dio->buff->pg_cnt );
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


int defer_io_redirect_bio( defer_io_t* defer_io, struct bio *bio, sector_t sectStart, sector_t sectCount, struct request_queue *q, make_request_fn* TargetMakeRequest_fn, void* pTracker )
{
	defer_io_original_request_t* dio_orig_req;
#ifdef SNAPSTORE
	if (snapstore_device_is_corrupted( defer_io->snapstore_device ))
		return -ENODATA;
#else
	if (snapshotdata_IsCorrupted( defer_io->snapshotdata, defer_io->original_dev_id ))
		return -ENODATA;
#endif

	dio_orig_req = (defer_io_original_request_t*)queue_content_sl_new_opt( &defer_io->dio_queue, GFP_NOIO );
	if (dio_orig_req == NULL)
		return -ENOMEM;


	//copy data from bio to dio write buffer
	dio_orig_req->sect.ofs = sectStart;
	dio_orig_req->sect.cnt = sectCount;
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
#ifdef SNAPSTORE
	if (defer_io->snapstore_device)
		snapstore_device_print_state( defer_io->snapstore_device );
#else
	if (defer_io->snapshotdata)
		snapshotdata_print_state( defer_io->snapshotdata );
#endif
}

