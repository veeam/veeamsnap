#pragma once

#include "shared_resource.h"

#ifdef SNAPSTORE
#include "snapstore_device.h"
#else
#include "snapshotdata.h"
#endif

typedef struct defer_io_s
{
	shared_resource_t sharing_header;

	wait_queue_head_t queue_add_event;

	atomic_t queue_filling_count;
	wait_queue_head_t queue_throttle_waiter;

	dev_t original_dev_id;
	struct block_device*  original_blk_dev;

#ifdef SNAPSTORE
	snapstore_device_t* snapstore_device;
#else
	snapshotdata_t* snapshotdata;
#endif
	struct task_struct* dio_thread;

	void*  rangecopy_buff;
	size_t rangecopy_buff_size;

	struct rw_semaphore flush_lock;

	queue_sl_t dio_queue;

	atomic64_t state_bios_received;
	atomic64_t state_bios_processed;
	atomic64_t state_sectors_received;
	atomic64_t state_sectors_processed;
	atomic64_t state_sectors_copy_read;
}defer_io_t;


int defer_io_create( dev_t dev_id, struct block_device* blk_dev, defer_io_t** pp_defer_io );
int defer_io_destroy( defer_io_t* defer_io );

static inline defer_io_t* defer_io_get_resource( defer_io_t* defer_io )
{
	return (defer_io_t*)shared_resource_get( &defer_io->sharing_header );
}
static inline void defer_io_put_resource( defer_io_t* defer_io )
{
	shared_resource_put( &defer_io->sharing_header );
}

int defer_io_redirect_bio( defer_io_t* defer_io, struct bio *bio, sector_t sectStart, sector_t sectCount, struct request_queue *q, make_request_fn* TargetMakeRequest_fn, void* pTracker );

void defer_io_print_state( defer_io_t* defer_io );
