#include "stdafx.h"
#include <asm/div64.h>

static inline unsigned long int do_div_inline( unsigned long long int division, unsigned long int divisor )
{
	unsigned long int result;

	result = do_div( division, divisor );

	return result;
}
//////////////////////////////////////////////////////////////////////////

#include "container.h"
#include "container_spinlocking.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"

#include "tracker_queue.h"
#include "range.h"
#include "rangeset.h"
#include "rangelist.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "shared_resource.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"
#include "defer_io.h"
#include "snapshot.h"
#include "cbt_map.h"
#include "veeamsnap_ioctl.h"
#include "tracker.h"

#include "bitmap_sync.h"
#include "veeamsnap_ioctl.h"
#include "snapimage.h"

static int g_snapimage_major = 0;
static bitmap_sync_t g_snapimage_minors;

static container_t g_SnapImages;
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
typedef struct snapimage_s{
	content_t content;

	sector_t capacity;
	dev_t original_dev;

	defer_io_t*	defer_io;
	cbt_map_t* cbt_map;

	dev_t image_dev;

	spinlock_t queue_lock;		// For exclusive access to our request queue
	struct request_queue* queue;
	struct gendisk*	disk;

	atomic_t own_cnt;

	queue_sl_t rq_proc_queue;

	struct task_struct* rq_processor;

	wait_queue_head_t	rq_proc_event;
	wait_queue_head_t	rq_complete_event;

	atomic64_t state_received;

	atomic64_t state_inprocess;
	atomic64_t state_processed;

	volatile sector_t last_read_sector;
	volatile sector_t last_read_size;
	volatile sector_t last_write_sector;
	volatile sector_t last_write_size;

	volatile struct block_device* open_bdev;
}snapimage_t;


//////////////////////////////////////////////////////////////////////////
int __snapimage_open( struct block_device *bdev, fmode_t mode )
{
	snapimage_t* image;

	if (bdev->bd_disk == NULL)
		return -ENODEV;

	image = bdev->bd_disk->private_data;
	if (image == NULL)
		return -ENODEV;

	image->open_bdev = bdev;

	log_traceln_dev_t( "dev=", image->image_dev );
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
int __snapimage_release( struct gendisk *disk, fmode_t mode )
#else
void __snapimage_release( struct gendisk *disk, fmode_t mode )
#endif
{
	log_traceln_dev_t( "dev id=", MKDEV(disk->major, disk->first_minor) );

	if (disk->private_data != NULL){
		snapimage_t* image = disk->private_data;

		image->open_bdev = NULL;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	return 0;
#endif
}


int __snapimage_ioctl( struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg )
{
	int res = -ENOTTY;
	snapimage_t* image = bdev->bd_disk->private_data;
	log_traceln_dev_t( "dev=", image->image_dev );


	switch (cmd) {
		/*
		* The only command we need to interpret is HDIO_GETGEO, since
		* we can't partition the drive otherwise.  We have no real
		* geometry, of course, so make something up.
		*/
	case HDIO_GETGEO:
	{
		struct hd_geometry geo;
		sector_t quotient;

		geo.start = 0;
		if (image->capacity > 63){

			geo.sectors = 63;
			quotient = do_div_inline( image->capacity + (63 - 1), 63 );

			if (quotient > 255ULL){
				geo.heads = 255;
				geo.cylinders = (unsigned short)do_div_inline( quotient + (255 - 1), 255 );
			}
			else{
				geo.heads = (unsigned char)quotient;
				geo.cylinders = 1;
			}
		}
		else{
			geo.sectors = (unsigned char)image->capacity;
			geo.cylinders = 1;
			geo.heads = 1;
		}
		log_traceln_sect( "capacity=", image->capacity );
		log_traceln_d( "heads=", geo.heads );
		log_traceln_d( "sectors=", geo.sectors );
		log_traceln_d( "cylinders=", geo.cylinders );
		log_traceln_ld( "start=", geo.start );

		if (copy_to_user( (void *)arg, &geo, sizeof( geo ) ))
			res = -EFAULT;
		else
			res = SUCCESS;
		break;
	}
	default:
		log_traceln_d( "cmd=", cmd );
		log_traceln_lx( "arg=", arg );
		res = -ENOTTY; /* unknown command */
	}
	return res;
}

#ifdef CONFIG_COMPAT
int __snapimage_compat_ioctl( struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg )
{
	snapimage_t* image = bdev->bd_disk->private_data;
	log_traceln_dev_t( "dev=", image->image_dev );
	log_traceln_d( "cmd=", cmd );
	log_traceln_lx( "arg=", arg );
	return -ENOTTY;
}
#endif

//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)

int __snapimage_open_2628( struct inode *inode, struct file *filp )
{
	return __snapimage_open( inode->i_bdev, 1 );
}
int __snapimage_release_2628( struct inode *inode, struct file *filp )
{
	return __snapimage_release( inode->i_bdev->bd_disk, 1 );
}
int __snapimage_ioctl_2628( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg )
{
	return __snapimage_ioctl( inode->i_bdev, 1, cmd, arg );

}

#ifdef CONFIG_COMPAT
int __snapimage_compat_ioctl_2628( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg )
{

}
#endif

static struct block_device_operations g_snapimage_ops = {
	.owner = THIS_MODULE,
	.open = __snapimage_open_2628,
	.ioctl = __snapimage_ioctl_2628,
	.release = __snapimage_release_2628,
#ifdef CONFIG_COMPAT
	.compat_ioctl = __snapimage_compat_ioctl_2628,
#endif
	.direct_access = NULL,
	//.check_events = NULL,
	.media_changed = NULL,
	//.unlock_native_capacity = NULL,
	.revalidate_disk = NULL,
	.getgeo = NULL,
	//.swap_slot_free_notify = NULL
};

#else //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)

static struct block_device_operations g_snapimage_ops = {
	.owner = THIS_MODULE,
	.open = __snapimage_open,
	.ioctl = __snapimage_ioctl,
	.release = __snapimage_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = __snapimage_compat_ioctl,
#endif
	.direct_access = NULL,
	//.check_events = NULL,
	.media_changed = NULL,
	//.unlock_native_capacity = NULL,
	.revalidate_disk = NULL,
	.getgeo = NULL,
	//.swap_slot_free_notify = NULL
};

#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)

//////////////////////////////////////////////////////////////////////////
#ifdef SNAPDATA_ZEROED
int __snapimage_read_blkdev_zeroing( redirect_bio_endio_t* rq_endio, struct block_device*  original_blk_dev, sector_t rq_pos, sector_t blk_ofs_start, sector_t blk_ofs_count, rangevector_t* zero_sectors )
{
	int res = SUCCESS;
	sector_t current_portion;
	sector_t ofs = 0;

	rangevector_el_t* el;

	sector_t from_sect;
	sector_t to_sect;

	RANGEVECTOR_READ_LOCK( zero_sectors );
	RANGEVECTOR_FOREACH_EL_BEGIN( zero_sectors, el )
	{
		range_t* first_zero_range;
		range_t* last_zero_range;
		size_t limit = (size_t)atomic_read( &el->cnt );
		if (limit <= 0)
			continue;

		first_zero_range = &el->ranges[0];
		last_zero_range = &el->ranges[limit - 1];

		from_sect = (rq_pos + blk_ofs_start + ofs);
		to_sect = (rq_pos + blk_ofs_start + blk_ofs_count);

		if ((last_zero_range->ofs + last_zero_range->cnt) <= from_sect){
			continue;
		}

		if (first_zero_range->ofs >= to_sect){
			break;
		}

		while (from_sect < to_sect){
			range_t* zero_range;
			zero_range = rangevector_el_find_first_hit( el, from_sect, to_sect );
			if (zero_range == NULL)
				break;

			if (zero_range->ofs > rq_pos + blk_ofs_start + ofs){
				sector_t pre_zero_cnt = zero_range->ofs - (rq_pos + blk_ofs_start + ofs);

				res = blk_dev_redirect_part( rq_endio, original_blk_dev, rq_pos + blk_ofs_start + ofs, blk_ofs_start + ofs, pre_zero_cnt );
				if (res != SUCCESS){
					break;
				}
				ofs += pre_zero_cnt;
			}

			current_portion = min_t( sector_t, zero_range->cnt, blk_ofs_count - ofs );

			res = blk_dev_zeroed_request_part( rq_endio, blk_ofs_start + ofs, current_portion );
			if (res != SUCCESS){
				break;
			}
			ofs += current_portion;

			from_sect = (rq_pos + blk_ofs_start + ofs);
		};
	}
	RANGEVECTOR_FOREACH_EL_END( zero_sectors );
	RANGEVECTOR_READ_UNLOCK( zero_sectors );
	if ((blk_ofs_count - ofs) > 0)
		res = blk_dev_redirect_part( rq_endio, original_blk_dev, rq_pos + blk_ofs_start + ofs, blk_ofs_start + ofs, blk_ofs_count - ofs );
	return res;
}
#endif //SNAPDATA_ZEROED

//////////////////////////////////////////////////////////////////////////
int __snapimage_request_read( defer_io_t* p_defer_io, redirect_bio_endio_t* rq_endio )
{
	int res = -ENODATA;

	bool is_redirected = false;

	bool is_snap_prev = false;          //previous state of block
	bool is_snap_curr = false;          //current block state

	sector_t blk_ofs_start = 0;         //device range start
	sector_t blk_ofs_count = 0;         //device range length
	sector_t block_ofs_curr = 0;
	sector_t len_sect;
	sector_t rq_pos;
	sector_t unordered_ofs;
#ifdef SNAPDATA_ZEROED
	rangevector_t* zero_sectors = NULL;
	if (get_zerosnapdata( ))
		zero_sectors = &p_defer_io->snapshotdata->zero_sectors;
#endif

	if (snapshotdata_IsCorrupted( p_defer_io->snapshotdata, p_defer_io->original_dev_id ))
		return -ENODATA;

	//enumarate state of all blocks for reading area.

	len_sect = sector_from_size( bio_bi_size( rq_endio->bio ) );
	rq_pos = bio_bi_sector( rq_endio->bio );

	if (!bio_has_data( rq_endio->bio )){
		log_warnln_sz( "bio empty. flags=", rq_endio->bio->bi_flags );
		rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, SUCCESS );
		queue_content_sl_free( &rq_endio->content );
		return SUCCESS;
	}

	unordered_ofs = (rq_pos + block_ofs_curr) & SNAPSHOTDATA_BLK_MASK;
	if (unlikely( unordered_ofs != 0 )){
		sector_t min_len = min_t( sector_t, SNAPSHOTDATA_BLK_SIZE - unordered_ofs, len_sect - block_ofs_curr );

		res = snapshotdata_TestBlockInBothMap( p_defer_io->snapshotdata, rq_pos + block_ofs_curr, &is_snap_curr );
		if (res != SUCCESS){
			log_errorln_sect( "TestBlock failed. pos=", rq_pos + block_ofs_curr );
			return res;
		}
		is_snap_prev = is_snap_curr;
		blk_ofs_start = block_ofs_curr;
		blk_ofs_count = min_len;

		block_ofs_curr += min_len;
	}

	for (; block_ofs_curr < len_sect; block_ofs_curr += SNAPSHOTDATA_BLK_SIZE){
		sector_t min_len = min_t( sector_t, SNAPSHOTDATA_BLK_SIZE, len_sect - block_ofs_curr );

		res = snapshotdata_TestBlockInBothMap( p_defer_io->snapshotdata, rq_pos + block_ofs_curr, &is_snap_curr );
		if (res != SUCCESS){
			log_errorln_sect( "TestBlock failed. pos=", rq_pos + block_ofs_curr );
			break;
		}

		if (is_snap_prev){

			if (is_snap_curr){
				blk_ofs_count += min_len;
			}
			else{
				if (blk_ofs_count){
					//snapshot read
					res = snapshotdata_read_part( p_defer_io->snapshotdata, rq_endio, rq_pos + blk_ofs_start, blk_ofs_start, blk_ofs_count );
					if (res != SUCCESS){
						log_errorln_d( "failed. err=", 0 - res );
						break;
					}
					is_redirected = true;
				}

				is_snap_prev = false;
				blk_ofs_start = block_ofs_curr;
				blk_ofs_count = min_len;
			}
		}
		else{
			if (is_snap_curr){
				if (blk_ofs_count){
#ifdef SNAPDATA_ZEROED
					//device read with zeroing
					if (zero_sectors)
						res = __snapimage_read_blkdev_zeroing( rq_endio, p_defer_io->original_blk_dev, rq_pos, blk_ofs_start, blk_ofs_count, zero_sectors );
					else
						res = blk_dev_redirect_part( rq_endio, p_defer_io->original_blk_dev, rq_pos + blk_ofs_start, blk_ofs_start, blk_ofs_count );
#else
					//device read
					res = blk_dev_redirect_part( rq_endio, p_defer_io->original_blk_dev, rq_pos + blk_ofs_start, blk_ofs_start, blk_ofs_count );
#endif
					if (res != SUCCESS){
						log_errorln_d( "failed. err=", 0 - res );
						break;
					}
					is_redirected = true;
				}

				is_snap_prev = true;
				blk_ofs_start = block_ofs_curr;
				blk_ofs_count = min_len;
			}
			else{
				//previous and current are not snapshot
				blk_ofs_count += min_len;
			}
		}
	}

	//read last blocks range
	if ((res == SUCCESS) || ( blk_ofs_count != 0 )){
		if (is_snap_curr){
			res = snapshotdata_read_part( p_defer_io->snapshotdata, rq_endio, rq_pos + blk_ofs_start, blk_ofs_start, blk_ofs_count );
		}
		else{
#ifdef SNAPDATA_ZEROED
			//device read with zeroing
			if (zero_sectors)
				res = __snapimage_read_blkdev_zeroing( rq_endio, p_defer_io->original_blk_dev, rq_pos, blk_ofs_start, blk_ofs_count, zero_sectors );
			else
				res = blk_dev_redirect_part( rq_endio, p_defer_io->original_blk_dev, rq_pos + blk_ofs_start, blk_ofs_start, blk_ofs_count );
#else
			res = blk_dev_redirect_part( rq_endio, p_defer_io->original_blk_dev, rq_pos + blk_ofs_start, blk_ofs_start, blk_ofs_count );
#endif
		}
		if (res == SUCCESS)
			is_redirected = true;
		else{
			log_errorln_d( "failed. err=", 0 - res );
		}
	}

	if ((res == SUCCESS) && is_redirected){
		if (atomic64_read( &rq_endio->bio_endio_count ) > 0ll) //async direct access needed
			blk_dev_redirect_submit( rq_endio );
		else{
			rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, res );
			queue_content_sl_free( &rq_endio->content );
		}
	}

	if (res == -ENODATA){
		log_errorln( "Nothing for read." );
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapimage_request_write( snapimage_t * image, redirect_bio_endio_t* rq_endio )
{
	int res;
	defer_io_t* p_defer_io = image->defer_io;
	cbt_map_t* cbt_map = image->cbt_map;

	if (!bio_has_data( rq_endio->bio )){
		log_warnln_sz( "bio empty. flags=", rq_endio->bio->bi_flags );
		rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, SUCCESS );
		queue_content_sl_free( &rq_endio->content );
		return SUCCESS;
	}

	res = snapshotdata_write_to_image( p_defer_io->snapshotdata, rq_endio->bio, p_defer_io->original_blk_dev );
	if (res != SUCCESS){
		log_errorln("Failed to write data to snapshot image." );
		return res;
	}
	if (cbt_map != NULL){
		sector_t ofs = bio_bi_sector( rq_endio->bio );
		sector_t cnt = sector_from_size( bio_bi_size( rq_endio->bio ) );
		res = cbt_map_set_previous_both( cbt_map, ofs, cnt );
	}
	rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, res );
	queue_content_sl_free( &rq_endio->content );

	return res;
}
//////////////////////////////////////////////////////////////////////////
void __snapimage_processing( snapimage_t * image )
{
	int res;
	redirect_bio_endio_t* rq_endio;

	atomic64_inc( &image->state_inprocess );
	rq_endio = (redirect_bio_endio_t*)queue_sl_get_first( &image->rq_proc_queue );

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	if (bio_data_dir( rq_endio->bio ) == READ){
#else
	if (!op_is_write( bio_op( rq_endio->bio ) )){
#endif
		{
			image->last_read_sector = bio_bi_sector( rq_endio->bio );
			image->last_read_size =  sector_from_uint( bio_bi_size( rq_endio->bio ) );
		}

		res = __snapimage_request_read( image->defer_io, rq_endio );
		if (res != SUCCESS){
			log_errorln_d( "Reading failed. res=", 0 - res );
		}
	}
	else{
		image->last_write_sector = bio_bi_sector( rq_endio->bio );
		image->last_write_size = sector_from_uint( bio_bi_size( rq_endio->bio ) );

		res = __snapimage_request_write( image, rq_endio );
		if (res != SUCCESS){
			log_errorln_d( "Writing failed. res=", 0 - res );
		}
	}

	if (res != SUCCESS){
		rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, res );
		queue_content_sl_free( &rq_endio->content );
	}
}
//////////////////////////////////////////////////////////////////////////

int snapimage_processor_waiting( snapimage_t *image )
{
	int res = SUCCESS;

	if (queue_sl_empty( image->rq_proc_queue )){
		res = wait_event_interruptible_timeout( image->rq_proc_event, (!queue_sl_empty( image->rq_proc_queue ) || kthread_should_stop( )), 5 * HZ );
		if (res > 0){
			res = SUCCESS;
		}
		else if (res == 0){
			res = -ETIME;
		}
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapimage_processor_thread( void *data )
{

	snapimage_t *image = data;

	log_traceln( "started." );

	//priority
	set_user_nice( current, -20 ); //MIN_NICE

	while ( !kthread_should_stop( ) )
	{
		int res = snapimage_processor_waiting( image );
		if (res == SUCCESS){
			if (!queue_sl_empty( image->rq_proc_queue ))
				__snapimage_processing( image );
		} else if (res == -ETIME){
			//Nobody read me
		}
		else{
			log_errorln_d( "Failed to wait queue.", 0 - res );
			return res;
		}
		schedule( );
	}

	while (!queue_sl_empty( image->rq_proc_queue ))
		__snapimage_processing( image );

	log_traceln( "stopped." );
	return 0;
}
//////////////////////////////////////////////////////////////////////////
static inline void __snapimage_bio_complete( struct bio* bio, int err )
{
	blk_bio_end( bio, err );
	bio_put( bio );
}

void __snapimage_bio_complete_cb( void* complete_param, struct bio* bio, int err )
{
	snapimage_t* image = (snapimage_t*)complete_param;

	atomic64_inc( &image->state_processed );

	__snapimage_bio_complete( bio, err );

	if (queue_sl_unactive( image->rq_proc_queue )){
		wake_up_interruptible( &image->rq_complete_event );
	}

	atomic_dec( &image->own_cnt );

}
//////////////////////////////////////////////////////////////////////////

int __snapimage_throttling( defer_io_t* defer_io )
{
	//wait_event_interruptible_timeout( defer_io->queue_throttle_waiter, (0 == atomic_read( &defer_io->queue_filling_count )), VEEAMIMAGE_THROTTLE_TIMEOUT );
	return wait_event_interruptible( defer_io->queue_throttle_waiter, queue_sl_empty( defer_io->dio_queue ) );
}

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 4, 0 )

#ifdef HAVE_MAKE_REQUEST_INT
int __snapimage_make_request( struct request_queue *q, struct bio *bio )
#else
void __snapimage_make_request( struct request_queue *q, struct bio *bio )
#endif

#else
blk_qc_t __snapimage_make_request( struct request_queue *q, struct bio *bio )
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 4, 4, 0 )
	blk_qc_t result = SUCCESS;
#endif
	snapimage_t* image = q->queuedata;
	redirect_bio_endio_t* rq_endio;

	bio_get( bio );

	if (q->queue_flags & ((1<<QUEUE_FLAG_STOPPED) | (1<<QUEUE_FLAG_DEAD))){
		log_traceln_lx( "Request failed. Queue already is not active. queue_flags=", q->queue_flags );
		__snapimage_bio_complete( bio, -EIO );

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 4, 0 )

#ifdef HAVE_MAKE_REQUEST_INT
		return 0;
#else
		return;
#endif

#else
		return result;
#endif
	}

	atomic_inc( &image->own_cnt );
	do{

		if (false == atomic_read( &(image->rq_proc_queue.active_state) )){
			__snapimage_bio_complete( bio, -EIO );
			break;
		}
		if (snapshotdata_IsCorrupted( image->defer_io->snapshotdata, image->defer_io->original_dev_id )){
			__snapimage_bio_complete( bio, -ENODATA );

			break;
		}
		{
			int res = __snapimage_throttling( image->defer_io );
			if (SUCCESS != res){
				log_errorln_d( "snapimage throttling failed. code=", 0-res );
				__snapimage_bio_complete( bio, res );
				break;
			}
		}

		while (NULL == (rq_endio = (redirect_bio_endio_t*)queue_content_sl_new_opt( &image->rq_proc_queue, GFP_KERNEL | __GFP_NOWARN ))){
			log_errorln( "Cannot allocate rq_endio. schedule and retry." );
			schedule( );
		}

		rq_endio->bio = bio;
		rq_endio->complete_cb = __snapimage_bio_complete_cb;
		rq_endio->complete_param = (void*)image;
		atomic_inc( &image->own_cnt );

		atomic64_inc( &image->state_received );

		if (SUCCESS == queue_sl_push_back( &image->rq_proc_queue, &rq_endio->content )){
			wake_up( &image->rq_proc_event );
		}
		else{
			queue_content_sl_free( &rq_endio->content );
			__snapimage_bio_complete( bio, -EIO );

			if (queue_sl_unactive( image->rq_proc_queue )){
				wake_up_interruptible( &image->rq_complete_event );
			}
		}

	}while (false);
	atomic_dec( &image->own_cnt );

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 4, 0 )

#ifdef HAVE_MAKE_REQUEST_INT
	return 0;
#endif

#else
	return result;
#endif
}


//////////////////////////////////////////////////////////////////////////
void __snapimage_destroy( snapimage_t* image );
//////////////////////////////////////////////////////////////////////////

static inline void __snapimage_free( snapimage_t* image )
{
	log_traceln_d( "image owning counter =  ", atomic_read( &image->own_cnt ) );

	defer_io_put_resource( image->defer_io );
	cbt_map_put_resource( image->cbt_map );
	image->defer_io = NULL;
}
//////////////////////////////////////////////////////////////////////////
int snapimage_create( dev_t original_dev )
{
	int res = SUCCESS;
	defer_io_t*	defer_io = NULL;
	cbt_map_t* cbt_map = NULL;
	snapimage_t* image = NULL;
	struct gendisk *disk = NULL;
	int minor;
	blk_dev_info_t original_dev_info;

	log_traceln_dev_t( "original_dev=", original_dev );

	res = blk_dev_get_info( original_dev, &original_dev_info );
	if (res != SUCCESS){
		log_errorln( "Failed to obtain original device info." );
		return res;
	}

	{
		tracker_t* pTracker = NULL;
		res = tracker_FindByDevId( original_dev, &pTracker );
		if (res != SUCCESS){
			log_errorln( "." );
			return res;
		}
		defer_io = pTracker->defer_io;
		cbt_map = pTracker->cbt_map;
	}
	image = (snapimage_t*)content_new( &g_SnapImages );
	if (image == NULL){
		log_errorln("Failed to allocate " );
		return -ENOMEM;
	}

	do{
		minor = bitmap_sync_find_clear_and_set( &g_snapimage_minors );
		if (minor < SUCCESS){
			log_errorln_d( "Failed to allocate minor.", 0-minor );
			break;
		}

		image->rq_processor = NULL;
		atomic64_set( &image->state_received, 0 );
		atomic64_set( &image->state_inprocess, 0 );
		atomic64_set( &image->state_processed, 0 );

		image->capacity = original_dev_info.count_sect;

		image->defer_io = defer_io_get_resource( defer_io );
		image->cbt_map = cbt_map_get_resource( cbt_map );
		image->original_dev = original_dev;

		image->image_dev = MKDEV( g_snapimage_major, minor );
		log_traceln_dev_t( "image_dev=", image->image_dev );

		atomic_set( &image->own_cnt, 0 );

		// queue with per request processing
		spin_lock_init( &image->queue_lock );

		image->queue = blk_init_queue( NULL, &image->queue_lock );
		if (NULL == image->queue){
			log_errorln( "blk_init_queue failure" );
			res = -ENOMEM;
			break;
		}
		image->queue->queuedata = image;

 		blk_queue_make_request( image->queue, __snapimage_make_request );
		blk_queue_max_segment_size( image->queue, 1024 * PAGE_SIZE );

		{
			unsigned int physical_block_size = original_dev_info.physical_block_size;
			unsigned short logical_block_size = original_dev_info.logical_block_size;//SECTOR512;

			log_traceln_d( "physical_block_size=", physical_block_size );
			log_traceln_d( "logical_block_size=", logical_block_size );

			blk_queue_physical_block_size( image->queue, physical_block_size );
			blk_queue_logical_block_size( image->queue, logical_block_size );
		}
		disk = alloc_disk( 1 );//only one partition on disk
		if (disk == NULL){
			log_errorln( "Cannot allocate disk." );
			res = -ENOMEM;
			break;
		}
		image->disk = disk;

		if (sprintf( disk->disk_name, "%s%d", VEEAM_SNAP_IMAGE, minor ) >= DISK_NAME_LEN){
			log_errorln_d( "Cannot set disk name. Invalid minor ", minor );
			res = -EINVAL;
			break;
		}

		log_traceln_s( "device name=", disk->disk_name );
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
		disk->flags |= GENHD_FL_NO_PART_SCAN;
		//disk->flags |= GENHD_FL_NATIVE_CAPACITY;
		//disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
#endif

		disk->major = g_snapimage_major;
		disk->minors = 1;    // one disk have only one partition.
		disk->first_minor = minor;

		disk->private_data = image;

		disk->fops = &g_snapimage_ops;
		disk->queue = image->queue;

		set_capacity( disk, image->capacity );
		log_traceln_lld( "capacity=", sector_to_streamsize(image->capacity) );

		res = queue_sl_init( &image->rq_proc_queue, sizeof( redirect_bio_endio_t ), disk->disk_name );
		if (res != SUCCESS){
			log_errorln_d( "Failed to initialize request processing queue. res=", 0 - res );
			break;
		}

		{
			struct task_struct* task = kthread_create( snapimage_processor_thread, image, disk->disk_name );
			if (IS_ERR( task )) {
				res = PTR_ERR( task );
				log_errorln_d( "Failed to allocate request processing thread. res=", 0 - res );
				break;
			}
			image->rq_processor = task;
		}
		init_waitqueue_head( &image->rq_complete_event );

		init_waitqueue_head( &image->rq_proc_event );
		wake_up_process( image->rq_processor );

		add_disk( disk );

	} while (false);

	if (res == SUCCESS){
		container_push_back( &g_SnapImages, &image->content );
	}
	else{

		__snapimage_destroy( image );
		__snapimage_free( image );
		content_free( &image->content );
		image = NULL;

	}
	return res;
}
void __snapimage_stop( snapimage_t* image )
{
	if (queue_sl_active( &image->rq_proc_queue, false )){
		unsigned long flags;
		struct request_queue* q = image->queue;

		if (blk_queue_stopped( q ))
			return;

		blk_sync_queue( q );

		spin_lock_irqsave( q->queue_lock, flags );
		blk_stop_queue( q );
		spin_unlock_irqrestore( q->queue_lock, flags );

	}
}

void __snapimage_destroy( snapimage_t* image )
{
	if (image->disk != NULL){
		log_traceln( "delete disk." );
		del_gendisk( image->disk );
	}

	if (image->rq_processor != NULL){
		__snapimage_stop( image );

		log_traceln( "stop request processor." );

		kthread_stop( image->rq_processor );
		image->rq_processor = NULL;

		while (!queue_sl_unactive( image->rq_proc_queue )){
			wait_event_interruptible( image->rq_complete_event, queue_sl_unactive( image->rq_proc_queue ) );
		};
	}
	do{
		if (image->queue) {
			log_traceln( "cleanup queue." );
			blk_cleanup_queue( image->queue );
			image->queue = NULL;
		}

		if (image->disk != NULL){
			image->disk->private_data = NULL;

			log_traceln( "release disk structure." );
			put_disk( image->disk );
			image->disk = NULL;
		}
		queue_sl_done( &image->rq_proc_queue );

		bitmap_sync_clear( &g_snapimage_minors, MINOR( image->image_dev ) );
	} while (false);
}

int snapimage_stop( dev_t original_dev )
{
	int res = SUCCESS;
	content_t* content = NULL;
	snapimage_t* image = NULL;

	log_traceln_dev_t( "original_dev=", original_dev );

	CONTAINER_FOREACH_BEGIN( g_SnapImages, content ){
		if (((snapimage_t*)content)->original_dev == original_dev){
			image = (snapimage_t*)content;
			break;
		}
	}CONTAINER_FOREACH_END( g_SnapImages );
	if (image != NULL){

		__snapimage_stop( image );

		res = SUCCESS;
	}
	else{
		log_errorln_d( "Snapshot image isn`t removed. status=", res );
		res = -ENODATA;
	}

	return res;
}
int snapimage_destroy( dev_t original_dev )
{
	int res = SUCCESS;
	content_t* content = NULL;
	snapimage_t* image = NULL;

	log_traceln_dev_t( "original_dev=", original_dev );

	CONTAINER_FOREACH_BEGIN( g_SnapImages, content ){
		if ( ((snapimage_t*)content)->original_dev == original_dev){
			image = (snapimage_t*)content;
			break;
		}
	}CONTAINER_FOREACH_END( g_SnapImages );
	if (image != NULL){

		__snapimage_destroy( image );
		__snapimage_free( image );
		container_free( &image->content );

		res = SUCCESS;
	}
	else{
		log_errorln_d( "Snapshot image isn`t removed. status=", res );
		res = -ENODATA;
	}

	return res;
}

int snapimage_destroy_for( dev_t* p_dev, int count )
{
	int res = SUCCESS;
	int inx = 0;
	log_traceln( "." );
	for (; inx < count; ++inx){
		int local_res = snapimage_destroy( p_dev[inx] );
		if (local_res != SUCCESS){
			log_errorln_dev_t( "Failed to release snapshot device for original=", p_dev[inx] );
			log_errorln_d( "", 0-local_res );
			res = local_res;
		}
	}
	return res;
}

int snapimage_create_for( dev_t* p_dev, int count )
{
	int res = SUCCESS;
	int inx = 0;

	for (; inx < count; ++inx){
		res = snapimage_create( p_dev[inx] );
		if (res != SUCCESS){
			log_errorln_dev_t( "Failed to create snapshot device for original=", p_dev[inx] );
			break;
		}
	}
	if (res != SUCCESS)
		if (inx > 0)
			snapimage_destroy_for( p_dev, inx-1 );
	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapimage_init( void )
{
	int res;
	log_traceln( "." );

	res = register_blkdev( g_snapimage_major, VEEAM_SNAP_IMAGE );
	if (res >= SUCCESS){
		g_snapimage_major = res;
		log_traceln_d( "Snapimage block device was registered. major=", g_snapimage_major );
		res = SUCCESS;

		res = container_init( &g_SnapImages, sizeof( snapimage_t ), NULL);
		if (res == SUCCESS){
			res = bitmap_sync_init( &g_snapimage_minors, SNAPIMAGE_MAX_DEVICES );
			if (res != SUCCESS){
				log_errorln( "Failed to initialize bitmap of minors." );
			}
		}
		else{
			log_errorln("Cannot create container Snapimages.");
		}
	}
	else{
		log_errorln_d( "Failed to register snapimage block device. major=", g_snapimage_major );
	}

	return res;
}

int snapimage_done( void )
{
	int res = SUCCESS;
	content_t* content = NULL;

	log_traceln( "." );

	while (NULL != (content = container_get_first( &g_SnapImages )))
	{
		snapimage_t* image = (snapimage_t*)content;

		log_errorln_dev_t( "unexpected snapshot image removing. original device=", image->original_dev );

		__snapimage_destroy( image );
		__snapimage_free( image );
		content_free( &image->content );
	}

	bitmap_sync_done( &g_snapimage_minors );

	res = container_done( &g_SnapImages );
	if (res != SUCCESS){
		log_errorln("Failed to release SnapImages container." );
	}

	unregister_blkdev( g_snapimage_major, VEEAM_SNAP_IMAGE );
	log_traceln_d( "Snapimage block device was unregistered. major=", g_snapimage_major );

	return res;
}

int snapimage_collect_images( int count, struct image_info_s* p_user_image_info, int* p_real_count )
{
	int res = SUCCESS;
	int real_count;

	log_traceln_d( "count=", count );

	real_count = container_length( &g_SnapImages );
	*p_real_count = real_count;
	log_traceln_d( "real_count=", real_count );

	if (count < real_count){
		res = -ENODATA;
	}
	real_count = min( count, real_count );
	if (real_count > 0){
		struct image_info_s* p_kernel_image_info = NULL;
		content_t* content;
		size_t inx = 0;
		size_t buff_size;

		buff_size = sizeof( struct image_info_s )*real_count;
		p_kernel_image_info = dbg_kzalloc( buff_size, GFP_KERNEL );
		if (p_kernel_image_info == NULL){
			log_errorln_sz( "Failed to allocate memory. Size=", buff_size );
			return res = -ENOMEM;
		}

		CONTAINER_FOREACH_BEGIN( g_SnapImages, content ){
			snapimage_t* img = (snapimage_t*)content;
			p_kernel_image_info[inx].original_dev_id.major = MAJOR( img->original_dev );
			p_kernel_image_info[inx].original_dev_id.minor = MINOR( img->original_dev );

			p_kernel_image_info[inx].snapshot_dev_id.major = MAJOR( img->image_dev );
			p_kernel_image_info[inx].snapshot_dev_id.minor = MINOR( img->image_dev );
			++inx;
			if (inx > real_count)
				break;
		}
		CONTAINER_FOREACH_END( g_SnapImages );

		{
			int left_data_length = copy_to_user( p_user_image_info, p_kernel_image_info, buff_size );
			if (left_data_length != 0){
				log_errorln_d( "Cannot copy data to user buffer ", (int)left_data_length );
				res = - ENODATA;
			}
		}

		dbg_kfree( p_kernel_image_info );
	}

	return res;
}

void snapimage_print_state( void )
{
	content_t* pCnt = NULL;

	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );

	CONTAINER_FOREACH_BEGIN( g_SnapImages, pCnt ){
		snapimage_t* image = (snapimage_t*)pCnt;
		pr_warn( "image=%p\n", (void*)image );
		pr_warn( "original_dev =%d.%d\n", MAJOR( image->original_dev ), MINOR( image->original_dev ) );
		pr_warn( "request: inprocess=%lld processed=%lld \n",
			(long long int)atomic64_read( &image->state_inprocess ),
			(long long int)atomic64_read( &image->state_processed ) );
		pr_warn( "image owning counter=%d\n", atomic_read( &image->own_cnt ) );
		pr_warn( "in queue: %d \n",	queue_sl_length( image->rq_proc_queue ) );
		pr_warn( "queue allocated: %d \n", atomic_read( &image->rq_proc_queue.alloc_cnt ) );

		pr_warn( "last read: sector=%lld count=%lld \n",
			(long long int)image->last_read_sector, (long long int)image->last_read_size );
		pr_warn( "last write: sector=%lld count=%lld \n",
			(long long int)image->last_write_sector, (long long int)image->last_write_size );

	}CONTAINER_FOREACH_END( g_SnapImages );
}
//////////////////////////////////////////////////////////////////////////
