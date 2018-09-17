#include "stdafx.h"
#include "blk_util.h"
#include "blk_redirect.h"
#include "blk_direct.h"

//#define _TRACE
struct bio_set* BlkRedirectBioset = NULL;

int blk_redirect_bioset_create( void )
{
	int ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )
	BlkRedirectBioset = bioset_create( 64, 0 );
#elif LINUX_VERSION_CODE > KERNEL_VERSION( 4, 17, 19 )
	BlkRedirectBioset = kzalloc(sizeof(*BlkRedirectBioset), GFP_KERNEL);
	if (!BlkRedirectBioset)
		return -ENOMEM;
	ret = bioset_init(BlkRedirectBioset, 64, 0, BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER );
	if (ret){
		kfree(BlkRedirectBioset);
		return ret;
	}
#else
	BlkRedirectBioset = bioset_create( 64, 0, BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER );
#endif

	if (BlkRedirectBioset == NULL){
		log_errorln( "Failed to create bio set." );
		return -ENOMEM;
	}
	log_traceln( "Specific bio set created." );
	return SUCCESS;
}

void blk_redirect_bioset_free( void )
{
	if (BlkRedirectBioset != NULL){
		bioset_exit( BlkRedirectBioset );
		BlkRedirectBioset = NULL;

		log_traceln( "Specific bio set free." );
	}
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int blk_redirect_bio_endio( struct bio *bb, unsigned int size, int err )
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void blk_redirect_bio_endio( struct bio *bb, int err )
#else
void blk_redirect_bio_endio( struct bio *bb )
#endif
{
	blk_redirect_bio_endio_t* rq_endio = (blk_redirect_bio_endio_t*)bb->bi_private;

	if (rq_endio != NULL){
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
		int err = SUCCESS;
#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )
		err = bb->bi_error;
#else
		if (bb->bi_status != BLK_STS_OK)
			err = -EIO;
#endif

#endif
		if (err != SUCCESS){
			log_errorln_d( "err=", 0 - err );
			log_errorln_sect( "offset=", bio_bi_sector( bb ) );

			if (rq_endio->err == SUCCESS)
				rq_endio->err = err;
		}

		if (atomic64_dec_and_test( &rq_endio->bio_endio_count ))
			blk_redirect_complete( rq_endio, rq_endio->err );
	}
	bio_put( bb );

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	return SUCCESS;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
void _blk_dev_redirect_bio_free( struct bio* bio )
{
	bio_free( bio, BlkRedirectBioset );
}
#endif

struct bio* _blk_dev_redirect_bio_alloc( int nr_iovecs, void* bi_private )
{
	struct bio* new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, BlkRedirectBioset );
	if (new_bio){
		new_bio->bi_end_io = blk_redirect_bio_endio;
		new_bio->bi_private = bi_private;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
		new_bio->bi_destructor = _blk_dev_redirect_bio_free;
#endif
	}
	return new_bio;
}

blk_redirect_bio_endio_list_t* _bio_endio_alloc_list( struct bio* new_bio )
{
	blk_redirect_bio_endio_list_t* next = dbg_kzalloc( sizeof( blk_redirect_bio_endio_list_t ), GFP_NOIO );
	if (next){
		next->next = NULL;
		next->this = new_bio;
#ifdef _TRACE
		log_errorln( "++" );
#endif
	}
	return next;
}

int  bio_endio_list_push( blk_redirect_bio_endio_t* rq_endio, struct bio* new_bio )
{
	blk_redirect_bio_endio_list_t* head;

	if (rq_endio->bio_endio_head_rec == NULL){
		if (NULL == (rq_endio->bio_endio_head_rec = _bio_endio_alloc_list( new_bio )))
			return -ENOMEM;
		return SUCCESS;
	}

	head = rq_endio->bio_endio_head_rec;
	while (head->next != NULL)
		head = head->next;

	if (NULL == (head->next = _bio_endio_alloc_list( new_bio )))
		return -ENOMEM;
	return SUCCESS;
}

void bio_endio_list_cleanup( blk_redirect_bio_endio_list_t* curr )
{
	while (curr != NULL){
		blk_redirect_bio_endio_list_t* next = curr->next;
		dbg_kfree( curr );
#ifdef _TRACE
		log_errorln( "--" );
#endif
		curr = next;
	}
}

int _blk_dev_redirect_part_read_fast( blk_redirect_bio_endio_t* rq_endio, int direction, struct block_device*  blk_dev, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
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
	blk_redirect_bio_endio_list_t* bio_endio_rec;

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
		sector_t bvec_ofs;
		sector_t bvec_sectors;

		if ((sect_ofs + bio_vec_sectors( bvec )) <= rq_ofs){
			sect_ofs += bio_vec_sectors( bvec );
			continue;
		}
		if (sect_ofs >= (rq_ofs + rq_count))
			break;

		bvec_ofs = 0;
		if (sect_ofs < rq_ofs)
			bvec_ofs = rq_ofs - sect_ofs;

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
			while (NULL == (new_bio = _blk_dev_redirect_bio_alloc( nr_iovecs, rq_endio ))){
				log_errorln( "cannot allocate new bio NOIO. Schedule." );
				schedule( );
			}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
			new_bio->bi_bdev = blk_dev;
#else
			new_bio->bi_disk = blk_dev->bd_disk;
			new_bio->bi_partno = blk_dev->bd_partno;
#endif

			if (direction == READ){
#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
				new_bio->bi_rw = READ;
#else
				bio_set_op_attrs( new_bio, REQ_OP_READ, 0 );
#endif
			}
			if (direction == WRITE){
#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
				new_bio->bi_rw = WRITE;
#else
				bio_set_op_attrs( new_bio, REQ_OP_WRITE, 0 );
#endif
			}
			bio_bi_sector( new_bio ) = target_pos + processed_sectors;// +bvec_ofs;
		}

		if (0 == bio_add_page( new_bio, bio_vec_page( bvec ), sector_to_uint( bvec_sectors ), bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ) )){
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

	log_errorln( "failed redirect_part" );
	log_errorln_sect( "    rq_ofs=", rq_ofs );
	log_errorln_sect( "    rq_count=", rq_count );
	return res;
}

int _blk_dev_redirect_part_read_sync( blk_redirect_bio_endio_t* rq_endio, int direction, struct block_device*  blk_dev, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	int result = SUCCESS;
	sector_t target_pos_ordered;
	sector_t rq_ofs_ordered;
	size_t page_count;
	page_array_t* arr = NULL;

	struct request_queue *q = bdev_get_queue( blk_dev );
	sector_t logical_block_size_mask = (sector_t)((q->limits.logical_block_size >> SECTOR512_SHIFT) - 1);

	log_errorln_sect( "target_pos= ", target_pos );
	log_errorln_sect( "rq_ofs= ", rq_ofs );
	log_errorln_sect( "rq_count= ", rq_count );

	target_pos_ordered = target_pos & ~logical_block_size_mask;
	rq_ofs_ordered = rq_ofs & ~logical_block_size_mask;
	//rq_count_ordered = rq_count & ~logical_block_size_mask;

	page_count = page_count_calculate( target_pos, rq_count );
	log_errorln_sz( "page_count= ", page_count );

	arr = page_array_alloc( page_count, GFP_NOIO );
	if (NULL == arr){
		log_errorln_sz( "Failed to allocate page buffer. Page count=", page_count );
		return -ENOMEM;
	}
	do{
		sector_t read_len;
		//log_errorln_sect( "target_pos_ordered= ", target_pos_ordered );
		//log_errorln_sect( "count= ", (sector_t)(page_count << (PAGE_SHIFT - SECTOR512_SHIFT)) );
		read_len = (sector_t)(page_count << (PAGE_SHIFT - SECTOR512_SHIFT));
		if (read_len != blk_direct_submit_pages( blk_dev, direction, 0, arr, target_pos_ordered, read_len )){
			log_errorln_sect( "Failed to read from offset: ", target_pos_ordered );
			result = -EIO;
			break;
		}

		{//copy data from pages array to request bio
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
			struct bio_vec* bvec;
			unsigned short iter;
#else
			struct bio_vec bvec;
			struct bvec_iter iter;
#endif
			sector_t sect_ofs = 0;
			sector_t processed_sectors = 0;
			size_t arr_ofs = (target_pos - target_pos_ordered) << SECTOR512_SHIFT;

			bio_for_each_segment( bvec, rq_endio->bio, iter ){
				sector_t bvec_ofs;
				sector_t bvec_sectors;

				if ((sect_ofs + bio_vec_sectors( bvec )) <= rq_ofs){
					sect_ofs += bio_vec_sectors( bvec );
					continue;
				}
				if (sect_ofs >= (rq_ofs + rq_count))
					break;

				bvec_ofs = 0;
				if (sect_ofs < rq_ofs)
					bvec_ofs = rq_ofs - sect_ofs;

				bvec_sectors = bio_vec_sectors( bvec ) - bvec_ofs;
				if (bvec_sectors >( rq_count - processed_sectors ))
					bvec_sectors = rq_count - processed_sectors;

				{
					void* mem = mem_kmap_atomic( bio_vec_page( bvec ) );

					log_errorln_sz( "bio page offset= ", (bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs )) );
					log_errorln_sect( "arr_ofs= ", arr_ofs );
					log_errorln_sz( "len= ", sector_to_uint( bvec_sectors ) );
					if (direction == READ){
						memcpy(
							mem + bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ),
							page_get_sector( arr, (arr_ofs >> SECTOR512_SHIFT) ),
							sector_to_uint( bvec_sectors ) );
					}
					if (direction == WRITE){
						memcpy(
							page_get_sector( arr, (arr_ofs >> SECTOR512_SHIFT) ),
							mem + bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ),
							sector_to_uint( bvec_sectors ) );
					}
					mem_kunmap_atomic( mem );
				}
				arr_ofs = sector_to_uint( bvec_sectors );
				processed_sectors += bvec_sectors;
				sect_ofs += bio_vec_sectors( bvec );
			}
		}
	} while (false);
	page_array_free( arr );

	return result;
}

int blk_dev_redirect_part( blk_redirect_bio_endio_t* rq_endio, int direction, struct block_device* blk_dev, sector_t target_pos, sector_t rq_ofs, sector_t rq_count )
{
	struct request_queue *q = bdev_get_queue( blk_dev );
	sector_t logical_block_size_mask = (sector_t)((q->limits.logical_block_size >> SECTOR512_SHIFT) - 1);

	//log_errorln_sect( "target_pos= ", target_pos );
	//log_errorln_sect( "rq_ofs= ", rq_ofs );
	//log_errorln_sect( "rq_count= ", rq_count );
	//log_errorln_sect( "logical_block_size_mask= ", logical_block_size_mask );

	if (likely( logical_block_size_mask == 0 ))
		return _blk_dev_redirect_part_read_fast( rq_endio, direction, blk_dev, target_pos, rq_ofs, rq_count );

	if (likely( (0 == (target_pos & logical_block_size_mask)) && (0 == (rq_count & logical_block_size_mask)) ))
		return _blk_dev_redirect_part_read_fast( rq_endio, direction, blk_dev, target_pos, rq_ofs, rq_count );

	return _blk_dev_redirect_part_read_sync( rq_endio, direction, blk_dev, target_pos, rq_ofs, rq_count );
}


void blk_dev_redirect_submit( blk_redirect_bio_endio_t* rq_endio )
{
	blk_redirect_bio_endio_list_t* head;
	blk_redirect_bio_endio_list_t* curr;

	head = curr = rq_endio->bio_endio_head_rec;
	rq_endio->bio_endio_head_rec = NULL;

	while (curr != NULL){
#ifndef REQ_OP_BITS //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
		submit_bio( bio_data_dir( rq_endio->bio ), curr->this );
#else
		submit_bio( curr->this );
#endif
		curr = curr->next;
	}

	bio_endio_list_cleanup( head );
}


int blk_dev_redirect_memcpy_part( blk_redirect_bio_endio_t* rq_endio, int direction, void* buff, sector_t rq_ofs, sector_t rq_count )
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
			if (direction == READ){
				memcpy(
					mem + bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ),
					buff + sector_to_uint( processed_sectors ),
					sector_to_uint( bvec_sectors ) );
			}
			else{
				memcpy(
					buff + sector_to_uint( processed_sectors ),
					mem + bio_vec_offset( bvec ) + sector_to_uint( bvec_ofs ),
					sector_to_uint( bvec_sectors ) );
			}
			mem_kunmap_atomic( mem );
		}

		processed_sectors += bvec_sectors;

		sect_ofs += bio_vec_sectors( bvec );
	}

	return SUCCESS;
}

int blk_dev_redirect_zeroed_part( blk_redirect_bio_endio_t* rq_endio, sector_t rq_ofs, sector_t rq_count )
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
/*
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
*/


#ifdef SNAPDATA_ZEROED
int blk_dev_redirect_read_zeroed( blk_redirect_bio_endio_t* rq_endio, struct block_device*  blk_dev, sector_t rq_pos, sector_t blk_ofs_start, sector_t blk_ofs_count, rangevector_t* zero_sectors )
{
	int res = SUCCESS;
	sector_t current_portion;
	sector_t ofs = 0;

	rangevector_el_t* el = NULL;

	sector_t from_sect;
	sector_t to_sect;

	BUG_ON( NULL == zero_sectors );

	RANGEVECTOR_READ_LOCK( zero_sectors );
	RANGEVECTOR_FOREACH_EL_BEGIN( zero_sectors, el )
	{
		range_t* first_zero_range;
		range_t* last_zero_range;
		size_t limit;

		limit = (size_t)atomic_read( &el->cnt );
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

				res = blk_dev_redirect_part( rq_endio, READ, blk_dev, rq_pos + blk_ofs_start + ofs, blk_ofs_start + ofs, pre_zero_cnt );
				if (res != SUCCESS){
					break;
				}
				ofs += pre_zero_cnt;
			}

			current_portion = min_t( sector_t, zero_range->cnt, blk_ofs_count - ofs );

			res = blk_dev_redirect_zeroed_part( rq_endio, blk_ofs_start + ofs, current_portion );
			if (res != SUCCESS){
				break;
			}
			ofs += current_portion;

			from_sect = (rq_pos + blk_ofs_start + ofs);
		};
	}
	RANGEVECTOR_FOREACH_EL_END( );
	RANGEVECTOR_READ_UNLOCK( zero_sectors );
	if ((blk_ofs_count - ofs) > 0)
		res = blk_dev_redirect_part( rq_endio, READ, blk_dev, rq_pos + blk_ofs_start + ofs, blk_ofs_start + ofs, blk_ofs_count - ofs );
	return res;
}

void blk_redirect_complete( blk_redirect_bio_endio_t* rq_endio, int res )
{
	rq_endio->complete_cb( rq_endio->complete_param, rq_endio->bio, res );
	queue_content_sl_free( &rq_endio->content );
}

#endif //SNAPDATA_ZEROED
