#pragma once

#ifndef BLK_DEV_ITILE_H
#define BLK_DEV_ITILE_H

#include "page_array.h"
#include "sector.h"
//////////////////////////////////////////////////////////////////////////
typedef struct blk_dev_info_s
{
	size_t blk_size;
	sector_t start_sect;
	sector_t count_sect;

	unsigned int io_min;
	unsigned int physical_block_size;
	unsigned short logical_block_size;

}blk_dev_info_t;
//////////////////////////////////////////////////////////////////////////
int blk_dev_open( dev_t dev_id, struct block_device** p_blk_dev );

void blk_dev_close( struct block_device* blk_dev );


int blk_dev_get_info( dev_t dev_id, blk_dev_info_t* pdev_info );
int __blk_dev_get_info( struct block_device* blk_dev, blk_dev_info_t* pdev_info );

int blk_freeze_bdev( dev_t dev_id, struct block_device* device, struct super_block** p_sb );
void blk_thaw_bdev( dev_t dev_id, struct block_device* device, struct super_block* sb );

//////////////////////////////////////////////////////////////////////////
typedef struct tracking_bio_complete_s { // like struct submit_bio_ret
	struct completion event;
	int error;
}tracking_bio_complete_t;
//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int tracking_bio_end_io( struct bio *bb, unsigned int size, int err );
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void tracking_bio_end_io( struct bio *bb, int err );
#else
void tracking_bio_end_io( struct bio *bb );
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
int tracking_redirect_bio_endio( struct bio *bb, unsigned int size, int err );
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
void tracking_redirect_bio_endio( struct bio *bb, int err );
#else
void tracking_redirect_bio_endio( struct bio *bb );
#endif


static __inline sector_t blk_dev_get_capacity( struct block_device* blk_dev )
{
	return blk_dev->bd_part->nr_sects;
};

static __inline sector_t blk_dev_get_start_sect( struct block_device* blk_dev )
{
	return blk_dev->bd_part->start_sect;
};


static __inline size_t blk_dev_get_block_size( struct block_device* blk_dev ){
	return (size_t)block_size( blk_dev );
}

static __inline void blk_bio_end( struct bio *bio, int err )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	bio_endio( bio, bio->bi_size, err );
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
	bio_endio( bio, err );
#else
	bio->bi_error = err;
	bio_endio( bio );
#endif
}
//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)

#define bio_vec_page(bv)    bv->bv_page
#define bio_vec_offset(bv)  bv->bv_offset
#define bio_vec_len(bv)     bv->bv_len
#define bio_vec_buffer(bv)  (page_address( bv->bv_page ) + bv->bv_offset)
#define bio_vec_sectors(bv) (bv->bv_len>>SECTOR512_SHIFT)

#define bio_bi_sector(bio)  bio->bi_sector
#define bio_bi_size(bio)    bio->bi_size

#else

#define bio_vec_page(bv)    bv.bv_page
#define bio_vec_offset(bv)  bv.bv_offset
#define bio_vec_len(bv)     bv.bv_len
#define bio_vec_buffer(bv)  (page_address( bv.bv_page ) + bv.bv_offset)
#define bio_vec_sectors(bv) (bv.bv_len>>SECTOR512_SHIFT)

#define bio_bi_sector(bio)  bio->bi_iter.bi_sector
#define bio_bi_size(bio)    bio->bi_iter.bi_size

#endif
//////////////////////////////////////////////////////////////////////////
static inline
sector_t blk_bio_io_vec_sectors( struct bio* bio )
{
	sector_t sect_cnt = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif
	bio_for_each_segment( bvec, bio, iter ){
		sect_cnt += ( bio_vec_len( bvec ) >> SECTOR512_SHIFT );
	}
	return sect_cnt;
}
//////////////////////////////////////////////////////////////////////////
typedef struct bio_endio_list_s{
	struct bio_endio_list_s* next;
	struct bio* this;
}bio_endio_list_t;

typedef void (redirect_bio_endio_complete_cb)( void* complete_param, struct bio* rq, int err );

typedef struct redirect_bio_endio_s{
	queue_content_sl_t content;

	struct bio *bio;
	int err;
	bio_endio_list_t* bio_endio_head_rec; //list of created bios
	atomic64_t bio_endio_count;

	void* complete_param;
	redirect_bio_endio_complete_cb* complete_cb;
}redirect_bio_endio_t;

//////////////////////////////////////////////////////////////////////////

int blk_dev_redirect_part( redirect_bio_endio_t* rq_endio, struct block_device*  blk_dev, sector_t target_pos, sector_t rq_ofs, sector_t rq_count );
void blk_dev_redirect_submit( redirect_bio_endio_t* rq_endio );

//int blk_dev_redirect_request( redirect_bio_endio_t* rq_endio, struct block_device*  blk_dev, sector_t target_pos );

int blk_dev_memcpy_request_part( redirect_bio_endio_t* rq_endio, void* src_buff, sector_t rq_ofs, sector_t rq_count );

int blk_dev_zeroed_request_part( redirect_bio_endio_t* rq_endio, sector_t rq_ofs, sector_t rq_count );
int blk_dev_zeroed_request( redirect_bio_endio_t* rq_endio );

//////////////////////////////////////////////////////////////////////////

sector_t blk_dev_direct_submit_pages( struct block_device* blkdev, int direction, sector_t arr_ofs, page_array_t* arr, sector_t ofs_sector, sector_t size_sector );
int blk_dev_direct_submit_page( struct block_device* blkdev, int direction, sector_t ofs_sect, struct page* pg );

//////////////////////////////////////////////////////////////////////////

int  blk_bioset_create( void );
void blk_bioset_free( void );
#endif//BLK_DEV_ITILE_H
