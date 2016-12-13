#include "stdafx.h"
#include "container.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"

struct bio_set* blk_bio_set = NULL;

//////////////////////////////////////////////////////////////////////////
int blk_bioset_create( void )
{
	blk_bio_set = bioset_create( 64, sizeof( tracking_bio_complete_t ) );
	if (blk_bio_set == NULL){
		log_errorln( "Failed to create bio set." );
		return -ENOMEM;
	}
	log_traceln( "Specific bio set created." );
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
void blk_bioset_free( void )
{
	if (blk_bio_set != NULL){
		bioset_free( blk_bio_set );
		blk_bio_set = NULL;

		log_traceln( "Specific bio set free." );
	}
}
//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int tracking_bio_end_io( struct bio *bb, unsigned int size, int err )
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void tracking_bio_end_io( struct bio *bb, int err )
#else
void tracking_bio_end_io( struct bio *bb )
#endif
{
	if (bb->bi_private){
		tracking_bio_complete_t* bio_compl = (tracking_bio_complete_t*)(bb->bi_private);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
		bio_compl->error = err;
#else
		bio_compl->error = bb->bi_error;
#endif
		complete( &(bio_compl->event) );
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	return err;
#endif
}
//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
void tracking_bio_free( struct bio* bio )
{
	bio_free( bio, blk_bio_set );
}
#endif
//////////////////////////////////////////////////////////////////////////

static inline
struct bio* blk_bio_alloc( int nr_iovecs )
{
	struct bio* new_bio = NULL;
	if (blk_bio_set != NULL){
		new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, blk_bio_set );
		if (new_bio){
			tracking_bio_complete_t* bio_compl = (tracking_bio_complete_t*)((void*)new_bio - sizeof( tracking_bio_complete_t ));
			bio_compl->error = -EINVAL;
			init_completion( &bio_compl->event );
			new_bio->bi_private = bio_compl;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
			new_bio->bi_destructor = tracking_bio_free;
#endif
		}
	}
	else
		new_bio = bio_alloc( GFP_NOIO, nr_iovecs );

	return new_bio;
}

//////////////////////////////////////////////////////////////////////////
int blk_dev_open( dev_t dev_id, struct block_device** p_blk_dev )
{
	int result = SUCCESS;
	struct block_device* blk_dev;
	int refCount;

	blk_dev = bdget( dev_id );
	if (NULL == blk_dev){
		log_errorln_dev_t( "dbget return zero for device=0x.", dev_id );
		return -ENODEV;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	refCount = blkdev_get( blk_dev, FMODE_READ | FMODE_WRITE, 0 );
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
	refCount = blkdev_get( blk_dev, FMODE_READ | FMODE_WRITE );
#else
	refCount = blkdev_get( blk_dev, FMODE_READ | FMODE_WRITE, NULL );
#endif
	if (refCount < 0){
		log_errorln_dev_t( "blkdev_get failed for device=", dev_id );
		result = refCount;
	}

	if (result == SUCCESS)
		*p_blk_dev = blk_dev;
	return result;
}
//////////////////////////////////////////////////////////////////////////
void blk_dev_close( struct block_device* blk_dev )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	blkdev_put( blk_dev );
#else
	blkdev_put( blk_dev, FMODE_READ );
#endif
}

int __blk_dev_get_info( struct block_device* blk_dev, blk_dev_info_t* pdev_info )
{
	sector_t SectorStart;
	sector_t SectorsCapacity;

	if (blk_dev->bd_part)
		SectorsCapacity = blk_dev->bd_part->nr_sects;
	else if (blk_dev->bd_disk)
		SectorsCapacity = get_capacity( blk_dev->bd_disk );
	else{
		return -EINVAL;
	}

	SectorStart = get_start_sect( blk_dev );

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
	if (blk_dev->bd_disk){
		pdev_info->physical_block_size = blk_dev->bd_disk->queue->limits.physical_block_size;
		pdev_info->logical_block_size = blk_dev->bd_disk->queue->limits.logical_block_size;
		pdev_info->io_min = blk_dev->bd_disk->queue->limits.io_min;
	}
	else{
		pdev_info->physical_block_size = SECTOR512;
		pdev_info->logical_block_size = SECTOR512;
		pdev_info->io_min = SECTOR512;
	}
#else
	pdev_info->physical_block_size = blk_dev->bd_queue->limits.physical_block_size;
	pdev_info->logical_block_size = blk_dev->bd_queue->limits.logical_block_size;
	pdev_info->io_min = blk_dev->bd_queue->limits.io_min;
#endif

	pdev_info->blk_size = blk_dev_get_block_size( blk_dev );
	pdev_info->start_sect = SectorStart;
	pdev_info->count_sect = SectorsCapacity;
	return SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
int blk_dev_get_info( dev_t dev_id, blk_dev_info_t* pdev_info )
{
	int result = SUCCESS;
	struct block_device* blk_dev;

	result = blk_dev_open( dev_id, &blk_dev );
	if (result != SUCCESS){
		log_errorln_dev_t( "Failed to open device=", dev_id );
		return result;
	}
	result = __blk_dev_get_info( blk_dev, pdev_info );
	if (result != SUCCESS){
		log_errorln_dev_t( "Cannot identify block device dev_id=0x", dev_id );
	}

	blk_dev_close( blk_dev );

	return result;
}

//////////////////////////////////////////////////////////////////////////
int blk_freeze_bdev( dev_t dev_id, struct block_device* device, struct super_block** p_sb )
{
	if (device->bd_super != NULL){
		*p_sb = freeze_bdev( device );
		if (NULL == *p_sb){
			log_errorln_dev_t( "freeze_bdev failed for device=", dev_id );
			return -ENODEV;
		}
	}
	else{
		log_traceln( "Device havn`t super block. It`s cannot be freeze." );
	}
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
void blk_thaw_bdev( dev_t dev_id, struct block_device* device, struct super_block* sb )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	thaw_bdev( device, sb );
	log_traceln_dev_t( "thaw_bdev for device=", dev_id );
#else
	if (SUCCESS < thaw_bdev( device, sb )){
		log_errorln_dev_t( "thaw_bdev failed for device=", dev_id );
	}
	else{
		log_traceln_dev_t( "thaw_bdev for device=", dev_id );
	}
#endif
}
//////////////////////////////////////////////////////////////////////////
bio_endio_list_t* __bio_endio_alloc_list( struct bio* new_bio )
{
	bio_endio_list_t* next = NULL;
	while (NULL == (next = dbg_kzalloc( sizeof( bio_endio_list_t ), GFP_NOIO ))){
		log_errorln( "cannot allocate memory NOIO. Schedule." );
		schedule( );
	}

	next->next = NULL;
	next->this = new_bio;

	return next;
}

//////////////////////////////////////////////////////////////////////////

int  bio_endio_list_push( redirect_bio_endio_t* rq_endio, struct bio* new_bio )
{
	bio_endio_list_t* head;

	if (rq_endio->bio_endio_head_rec == NULL){
		rq_endio->bio_endio_head_rec = __bio_endio_alloc_list( new_bio );;
		return SUCCESS;
	}

	head = rq_endio->bio_endio_head_rec;
	while (head->next != NULL)
		head = head->next;
	head->next = __bio_endio_alloc_list( new_bio );

	return SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
void bio_endio_list_cleanup( bio_endio_list_t* curr )
{
	while (curr != NULL){
		bio_endio_list_t* next = curr->next;
		dbg_kfree( curr );
		curr = next;
	}
}

//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int tracking_redirect_bio_endio( struct bio *bb, unsigned int size, int err )
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void tracking_redirect_bio_endio( struct bio *bb, int err )
#else
void tracking_redirect_bio_endio( struct bio *bb )
#endif
{
	redirect_bio_endio_t* rq_endio = (redirect_bio_endio_t*)bb->bi_private;

	if (rq_endio != NULL){
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
		int err = bb->bi_error;
#endif
		if (err != SUCCESS){
			log_errorln_d( "err=", 0 - err );
			log_errorln_sect( "offset=", bio_bi_sector( bb ) );

			if (rq_endio->err == SUCCESS)
				rq_endio->err = err;
		}

		if (atomic64_dec_and_test( &rq_endio->bio_endio_count )){
			rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, rq_endio->err );
			queue_content_sl_free( &rq_endio->content );
		}
	}
	bio_put( bb );

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	return SUCCESS;
#endif
}
//////////////////////////////////////////////////////////////////////////
int blk_dev_redirect_part( redirect_bio_endio_t* rq_endio, struct block_device*  blk_dev, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	__label__ __fail_out;
	__label__ __reprocess_bv;

	int res = SUCCESS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif

	struct bio* new_bio = NULL;

	sector_t sect_ofs = 0;
	sector_t processed_sectors = 0;
	int nr_iovecs;
	unsigned int max_sect;
	bio_endio_list_t* bio_endio_rec;

	{
		struct request_queue *q = bdev_get_queue( blk_dev );
#ifdef BIO_MAX_SECTORS
		max_sect = BIO_MAX_SECTORS;
#else
		max_sect = BIO_MAX_PAGES << ( PAGE_SHIFT - SECTOR512_SHIFT);
#endif
		max_sect = min( max_sect, q->limits.max_sectors );
	}

	nr_iovecs = max_sect >> (PAGE_SHIFT - SECTOR512_SHIFT);

	bio_for_each_segment(bvec, rq_endio->bio, iter ){
		sector_t bvec_ofs;
		sector_t bvec_sectors;

		if ((sect_ofs + bio_vec_sectors( bvec )) <= rq_ofs){
			sect_ofs += bio_vec_sectors( bvec );
			continue;
		}
		if (sect_ofs >= (rq_ofs + rq_count)){
			break;
		}

		bvec_ofs = 0;
		if (sect_ofs < rq_ofs){
			bvec_ofs = rq_ofs - sect_ofs;
		}

		bvec_sectors = bio_vec_sectors( bvec ) - bvec_ofs;
		if (bvec_sectors >( rq_count - processed_sectors ))
			bvec_sectors = rq_count - processed_sectors;

		if (bvec_sectors == 0){
			log_traceln( "bvec_sectors ZERO!" );

			log_traceln_sect( "bio_vec_sectors=", bio_vec_sectors( bvec ) );
			log_traceln_sect( "bvec_ofs=", bvec_ofs );
			log_traceln_sect( "rq_count=", rq_count );
			log_traceln_sect( "processed_sectors=", processed_sectors );


			res = -EIO;
			goto __fail_out;
		}

__reprocess_bv:
		if (new_bio == NULL){
			while (NULL == (new_bio = blk_bio_alloc( nr_iovecs ))){
				log_errorln( "cannot allocate new bio NOIO. Schedule." );
				schedule( );
			}

			new_bio->bi_bdev = blk_dev;
			new_bio->bi_end_io = tracking_redirect_bio_endio;
			new_bio->bi_private = rq_endio;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
			new_bio->bi_rw = READ;
#else
			bio_set_op_attrs( new_bio, REQ_OP_READ, 0 );
#endif

			bio_bi_sector( new_bio ) = target_pos + processed_sectors;// +bvec_ofs;
		}

		if (0 == bio_add_page( new_bio, bio_vec_page( bvec ), sector_to_uint( bvec_sectors ), bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs) ) ){
			if (bio_bi_size( new_bio ) == 0){
				res = -EIO;
				goto __fail_out;
			}

			res = bio_endio_list_push( rq_endio, new_bio );
			if (res != SUCCESS){
				log_traceln( "Failed to add bio to bio_endio_list." );
				goto __fail_out;
			}

			atomic64_inc( &rq_endio->bio_endio_count );
			new_bio = NULL;

			goto __reprocess_bv;
		}
		processed_sectors += bvec_sectors;

		sect_ofs += bio_vec_sectors( bvec );
	}

	if (new_bio != NULL){
		res = bio_endio_list_push( rq_endio, new_bio );
		if (res != SUCCESS){
			log_traceln( "Failed to add bio to bio_endio_list." );
			goto __fail_out;
		}

		atomic64_inc( &rq_endio->bio_endio_count );
		new_bio = NULL;
	}

	return SUCCESS;

__fail_out:
	bio_endio_rec = rq_endio->bio_endio_head_rec;
	while (bio_endio_rec != NULL){

		if (bio_endio_rec->this != NULL)
			bio_put( bio_endio_rec->this );

		bio_endio_rec = bio_endio_rec->next;
	}

	bio_endio_list_cleanup( bio_endio_rec );

	log_errorln_sect( "failed redirect_part. rq_ofs=", rq_ofs );
	log_errorln_sect( "failed redirect_part. rq_count=", rq_count );
	return res;
}

//////////////////////////////////////////////////////////////////////////
void blk_dev_redirect_submit( redirect_bio_endio_t* rq_endio )
{
	bio_endio_list_t* head;
	bio_endio_list_t* curr;

	head = curr = rq_endio->bio_endio_head_rec;
	rq_endio->bio_endio_head_rec = NULL;

	while (curr != NULL){
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
		submit_bio( bio_data_dir( rq_endio->bio ), curr->this );
#else
		submit_bio( curr->this );
#endif
		curr = curr->next;
	}

	bio_endio_list_cleanup( head );
}

//////////////////////////////////////////////////////////////////////////
/*
int blk_dev_redirect_request( redirect_bio_endio_t* rq_endio, struct block_device*  blk_dev, sector_t target_pos )
{
	__label__ __fail_out;
	__label__ __reprocess_bv;

	int res = SUCCESS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif
	struct bio* new_bio = NULL;

	sector_t sect_ofs = 0;
	int nr_iovecs;
	unsigned int max_sect;
	bio_endio_list_t* bio_endio_rec;

	{
		struct request_queue *q = bdev_get_queue( blk_dev );
#ifdef BIO_MAX_SECTORS
		max_sect = BIO_MAX_SECTORS;
#else
		max_sect = BIO_MAX_PAGES << (PAGE_SHIFT - SECTOR512_SHIFT);
#endif
		max_sect = min( max_sect, q->limits.max_sectors );
	}

	nr_iovecs = max_sect >> (PAGE_SHIFT - SECTOR512_SHIFT);

	bio_for_each_segment( bvec, rq_endio->bio, iter ){
__reprocess_bv:
		if (new_bio == NULL){
			while (NULL == (new_bio = blk_bio_alloc( nr_iovecs ))){
				log_errorln( "cannot allocate new bio NOIO. Schedule." );
				schedule( );
			}

			new_bio->bi_bdev = blk_dev;
			new_bio->bi_end_io = tracking_redirect_bio_endio;
			new_bio->bi_private = rq_endio;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
			new_bio->bi_rw = READ;
#else
			bio_set_op_attrs( new_bio, REQ_OP_READ, 0 );
#endif

			bio_bi_sector( new_bio ) = target_pos + sect_ofs;
		}

		if (0==bio_add_page( new_bio, bio_vec_page( bvec ), bio_vec_len( bvec ), bio_vec_offset( bvec ) )){

			log_traceln_d( "bvec full! bi_size=", bio_bi_size( new_bio ) );
			log_traceln_p( "bio add.", new_bio );

			if (bio_bi_size( new_bio ) == 0){
				res = -EIO;
				goto __fail_out;
			}

			res = bio_endio_list_push( rq_endio, new_bio );
			if (res != SUCCESS){
				log_traceln( "Failed to add bio to bio_endio_list." );
				goto __fail_out;
			}

			atomic64_inc( &rq_endio->bio_endio_count );
			new_bio = NULL;

			goto __reprocess_bv;
		}

		sect_ofs += sector_from_uint( bio_vec_len(bvec) );
	}

	if (new_bio != NULL){
		res = bio_endio_list_push( rq_endio, new_bio );
		if (res != SUCCESS){
			log_traceln( "Failed to add bio to bio_endio_list." );
			goto __fail_out;
		}

		atomic64_inc( &rq_endio->bio_endio_count );
		new_bio = NULL;
	}

	blk_dev_redirect_submit( rq_endio );

	return SUCCESS;

__fail_out:
	bio_endio_rec = rq_endio->bio_endio_head_rec;
	while (bio_endio_rec != NULL){

		if (bio_endio_rec->this != NULL)
			bio_put( bio_endio_rec->this );

		bio_endio_rec = bio_endio_rec->next;
	}

	bio_endio_list_cleanup( bio_endio_rec );
	return res;
}
*/
//////////////////////////////////////////////////////////////////////////
int blk_dev_memcpy_request_part( redirect_bio_endio_t* rq_endio, void* src_buff, sector_t rq_ofs, sector_t rq_count )
{

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif

	sector_t sect_ofs = 0;
	sector_t processed_sectors = 0;

	bio_for_each_segment( bvec, rq_endio->bio, iter ){
		sector_t bvec_ofs;
		sector_t bvec_sectors;

		if ((sect_ofs + bio_vec_sectors( bvec )) <= rq_ofs){
			sect_ofs += bio_vec_sectors( bvec );
			continue;
		}
		if (sect_ofs >= (rq_ofs + rq_count)){
			break;
		}

		bvec_ofs = 0;
		if (sect_ofs < rq_ofs){
			bvec_ofs = rq_ofs - sect_ofs;
		}

		bvec_sectors = bio_vec_sectors( bvec ) - bvec_ofs;
		if (bvec_sectors >( rq_count - processed_sectors ))
			bvec_sectors = rq_count - processed_sectors;

		{
			void* mem = mem_kmap_atomic( bio_vec_page( bvec ) );
			memcpy(
				mem + bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ),
				src_buff + sector_to_uint( processed_sectors),
				sector_to_uint( bvec_sectors ) );

			mem_kunmap_atomic( mem );
		}

		processed_sectors += bvec_sectors;

		sect_ofs += bio_vec_sectors( bvec );
	}

	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int blk_dev_zeroed_request_part( redirect_bio_endio_t* rq_endio, sector_t rq_ofs, sector_t rq_count )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif

	sector_t sect_ofs = 0;
	sector_t processed_sectors = 0;

	bio_for_each_segment( bvec, rq_endio->bio, iter ){
		sector_t bvec_ofs;
		sector_t bvec_sectors;

		if ((sect_ofs + bio_vec_sectors( bvec )) <= rq_ofs){
			sect_ofs += bio_vec_sectors( bvec );
			continue;
		}
		if (sect_ofs >= (rq_ofs + rq_count)){
			break;
		}

		bvec_ofs = 0;
		if (sect_ofs < rq_ofs){
			bvec_ofs = rq_ofs - sect_ofs;
		}

		bvec_sectors = bio_vec_sectors( bvec ) - bvec_ofs;
		if (bvec_sectors >( rq_count - processed_sectors ))
			bvec_sectors = rq_count - processed_sectors;

		{
			void* mem = mem_kmap_atomic( bio_vec_page( bvec ) );

			memset( mem + bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ), 0, sector_to_uint( bvec_sectors ) );

			mem_kunmap_atomic( mem );
		}

		processed_sectors += bvec_sectors;

		sect_ofs += bio_vec_sectors( bvec );
	}

	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int blk_dev_zeroed_request( redirect_bio_endio_t* rq_endio )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif

	bio_for_each_segment( bvec, rq_endio->bio, iter )
	{
		void* mem = mem_kmap_atomic( bio_vec_page( bvec ) );

		memset( mem + bio_vec_offset( bvec ), 0, bio_vec_len( bvec ) );

		mem_kunmap_atomic( mem );
	}

	rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, SUCCESS );
	queue_content_sl_free( &rq_endio->content );
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int __dev_direct_submit_pages(
    struct block_device* blkdev,
	int direction,
	sector_t arr_ofs,
	page_array_t* arr,
	sector_t ofs_sector,
	sector_t size_sector,
	sector_t* processed_sectors
){
	tracking_bio_complete_t* bio_compl = NULL;
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

	while (NULL == (bb = blk_bio_alloc( nr_iovecs ))){
		log_errorln_d( "bio_alloc failed. nr_iovecs=", nr_iovecs );
		log_errorln_sect( "ofs_sector=", ofs_sector );
		log_errorln_sect( "size_sector=", size_sector );

		*processed_sectors = 0;
		return -ENOMEM;
	}
	bio_compl = (tracking_bio_complete_t*)bb->bi_private;

	bb->bi_bdev = blkdev;
	bb->bi_end_io = tracking_bio_end_io;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
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
	while ((process_sect < size_sector) && (page_inx < arr->count))
	{
		sector_t bvec_len_sect = min_t( sector_t, SECTORS_IN_PAGE, (size_sector - process_sect) );

		if (0 == bio_add_page( bb, arr->pg[page_inx].page, sector_to_uint( bvec_len_sect ), 0 )){
			break;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
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
//////////////////////////////////////////////////////////////////////////
sector_t blk_dev_direct_submit_pages(
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

		result = __dev_direct_submit_pages( blkdev, direction, arr_ofs + process_sect, arr, ofs_sector + process_sect, size_sector - process_sect, &portion );
		if (SUCCESS != result)
			break;

		process_sect += portion;

	} while (process_sect < size_sector);

	return process_sect;
}

//////////////////////////////////////////////////////////////////////////
int blk_dev_direct_submit_page( struct block_device* blkdev, int direction, sector_t ofs_sect, struct page* pg)
{
	int res = -EIO;
	struct bio *bb = NULL;
	tracking_bio_complete_t* bio_compl = NULL;

	while (NULL == (bb = blk_bio_alloc( 1 ))){
		log_errorln( "bio_alloc failed. Schedule." );
		schedule( );
	}
	bio_compl = (tracking_bio_complete_t*)bb->bi_private;

	bb->bi_bdev = blkdev;
	bb->bi_end_io = tracking_bio_end_io;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	bb->bi_rw = direction;
#else
	if (direction == READ)
		bio_set_op_attrs( bb, REQ_OP_READ, 0 );
	else
		bio_set_op_attrs( bb, REQ_OP_WRITE, 0 );
#endif
	bio_bi_sector( bb ) = ofs_sect;

	if (0 != bio_add_page( bb, pg, PAGE_SIZE, 0 )){
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
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
//////////////////////////////////////////////////////////////////////////

