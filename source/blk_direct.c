#include "stdafx.h"
#include "blk_direct.h"


struct bio_set* BlkDirectBioset = NULL;

typedef struct blk_direct_bio_complete_s { // like struct submit_bio_ret
	struct completion event;
	int error;
}blk_direct_bio_complete_t;

int blk_direct_bioset_create( void )
{
	int ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )
	BlkDirectBioset = bioset_create( 64, sizeof( blk_direct_bio_complete_t ) );
#elif LINUX_VERSION_CODE > KERNEL_VERSION( 4, 17, 19 )
	BlkDirectBioset = kzalloc(sizeof(*BlkDirectBioset), GFP_KERNEL);
	if (!BlkDirectBioset)
		return -ENOMEM;
	ret = bioset_init(BlkDirectBioset, 64, sizeof( blk_direct_bio_complete_t ), BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER );
	if (ret){
		kfree(BlkDirectBioset);
		return ret;
	}
#else
	BlkDirectBioset = bioset_create( 64, sizeof( blk_direct_bio_complete_t ), BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER );
#endif
	if (BlkDirectBioset == NULL){
		log_errorln( "Failed to create bio set." );
		return -ENOMEM;
	}
	log_traceln( "Specific bio set created." );
	return SUCCESS;
}

void blk_direct_bioset_free( void )
{
	if (BlkDirectBioset != NULL){
		bioset_exit( BlkDirectBioset );
		BlkDirectBioset = NULL;

		log_traceln( "Specific bio set free." );
	}
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int blk_direct_bio_endio( struct bio *bb, unsigned int size, int err )
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void blk_direct_bio_endio( struct bio *bb, int err )
#else
void blk_direct_bio_endio( struct bio *bb )
#endif
{
	if (bb->bi_private){
		blk_direct_bio_complete_t* bio_compl = (blk_direct_bio_complete_t*)(bb->bi_private);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
		bio_compl->error = err;
#else

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )
		bio_compl->error = bb->bi_error;
#else
		if (bb->bi_status != BLK_STS_OK)
			bio_compl->error = -EIO;
		else
			bio_compl->error = SUCCESS;
#endif

#endif
		complete( &(bio_compl->event) );
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	return err;
#endif
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
void _blk_dev_direct_bio_free( struct bio* bio )
{
	bio_free( bio, BlkDirectBioset );
}
#endif

struct bio* _blk_dev_direct_bio_alloc( int nr_iovecs )
{
	struct bio* new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, BlkDirectBioset );
	if (new_bio){
		blk_direct_bio_complete_t* bio_compl = (blk_direct_bio_complete_t*)((void*)new_bio - sizeof( blk_direct_bio_complete_t ));
		bio_compl->error = -EINVAL;
		init_completion( &bio_compl->event );
		new_bio->bi_private = bio_compl;
		new_bio->bi_end_io = blk_direct_bio_endio;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
		new_bio->bi_destructor = _blk_dev_direct_bio_free;
#endif
	}
	return new_bio;
}


int _dev_direct_submit_pages(
	struct block_device* blkdev,
	int direction,
	sector_t arr_ofs,
	page_array_t* arr,
	sector_t ofs_sector,
	sector_t size_sector,
	sector_t* processed_sectors
){
	blk_direct_bio_complete_t* bio_compl = NULL;
	struct bio *bb = NULL;
	int nr_iovecs;
	int page_inx = arr_ofs / SECTORS_IN_PAGE;
	sector_t process_sect = 0;

	{
		struct request_queue *q = bdev_get_queue( blkdev );
		size_sector = min_t( sector_t, size_sector, q->limits.max_sectors );

#ifdef BIO_MAX_SECTORS
		size_sector = min_t( sector_t, size_sector, BIO_MAX_SECTORS );
#else
		size_sector = min_t( sector_t, size_sector, (BIO_MAX_PAGES << (PAGE_SHIFT - SECTOR512_SHIFT)) );
#endif

	}

	nr_iovecs = page_count_calculate( ofs_sector, size_sector );

	while (NULL == (bb = _blk_dev_direct_bio_alloc( nr_iovecs ))){
		log_errorln_d( "bio_alloc failed. nr_iovecs=", nr_iovecs );
		log_errorln_sect( "ofs_sector=", ofs_sector );
		log_errorln_sect( "size_sector=", size_sector );

		*processed_sectors = 0;
		return -ENOMEM;
	}
	bio_compl = (blk_direct_bio_complete_t*)bb->bi_private;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	bb->bi_bdev = blkdev;
#else
	bb->bi_disk = blkdev->bd_disk;
	bb->bi_partno = blkdev->bd_partno;
#endif

#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	bb->bi_rw = direction;
#else
	if (direction == READ)
		bio_set_op_attrs( bb, REQ_OP_READ, 0 );
	else
		bio_set_op_attrs( bb, REQ_OP_WRITE, 0 );
#endif
	bio_bi_sector( bb ) = ofs_sector;

	{
		sector_t unordered = arr_ofs & (SECTORS_IN_PAGE - 1);
		sector_t bvec_len_sect = min_t( sector_t, (SECTORS_IN_PAGE - unordered), size_sector );

		if (0 == bio_add_page( bb, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), sector_to_uint( unordered ) )){
			log_errorln_d( "bvec full! bi_size=", bio_bi_size( bb ) );
			goto blk_dev_direct_submit_pages_label_failed;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}
	while ((process_sect < size_sector) && (page_inx < arr->pg_cnt))
	{
		sector_t bvec_len_sect = min_t( sector_t, SECTORS_IN_PAGE, (size_sector - process_sect) );

		if (0 == bio_add_page( bb, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), 0 )){
			break;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}

#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	submit_bio( direction, bb );
#else
	submit_bio( bb );
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	wait_for_completion( &bio_compl->event );
#else
	wait_for_completion_io( &bio_compl->event );
#endif
	if (bio_compl->error != SUCCESS){
		log_errorln_d( "err=", bio_compl->error );
		process_sect = 0;
	}
blk_dev_direct_submit_pages_label_failed:
	bio_put( bb );

	*processed_sectors = process_sect;
	return SUCCESS;
}

sector_t blk_direct_submit_pages(
struct block_device* blkdev,
	int direction,
	sector_t arr_ofs,
	page_array_t* arr,
	sector_t ofs_sector,
	sector_t size_sector
	){
	sector_t process_sect = 0;
	do{
		int result;
		sector_t portion = 0;

		result = _dev_direct_submit_pages( blkdev, direction, arr_ofs + process_sect, arr, ofs_sector + process_sect, size_sector - process_sect, &portion );
		if (SUCCESS != result)
			break;

		process_sect += portion;

	} while (process_sect < size_sector);

	return process_sect;
}


int blk_direct_submit_page( struct block_device* blkdev, int direction, sector_t ofs_sect, struct page* pg )
{
	int res = -EIO;
	struct bio *bb = NULL;
	blk_direct_bio_complete_t* bio_compl = NULL;

	while (NULL == (bb = _blk_dev_direct_bio_alloc( 1 ))){
		log_errorln( "bio_alloc failed. Schedule." );
		schedule( );
	}
	bio_compl = (blk_direct_bio_complete_t*)bb->bi_private;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	bb->bi_bdev = blkdev;
#else
	bb->bi_disk = blkdev->bd_disk;
	bb->bi_partno = blkdev->bd_partno;
#endif

#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	bb->bi_rw = direction;
#else
	if (direction == READ)
		bio_set_op_attrs( bb, REQ_OP_READ, 0 );
	else
		bio_set_op_attrs( bb, REQ_OP_WRITE, 0 );
#endif
	bio_bi_sector( bb ) = ofs_sect;

	if (0 != bio_add_page( bb, pg, PAGE_SIZE, 0 )){
#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
		submit_bio( bb->bi_rw, bb );
#else
		submit_bio( bb );
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
		wait_for_completion( &bio_compl->event );
#else
		wait_for_completion_io( &bio_compl->event );
#endif

		res = bio_compl->error;
		if (bio_compl->error != SUCCESS){
			log_errorln_d( "err=", bio_compl->error );
		}
	}
	else{
		log_errorln_sz( "bio_add_page fail. PAGE_SIZE=", PAGE_SIZE );
	}
	bio_put( bb );
	return res;
}

