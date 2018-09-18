#pragma once

#ifndef SNAPSTORE

#include "container.h"
#include "blk_deferred.h"
#include "blk_redirect.h"
#include "snapshotdata_shared.h"
#include "snapshotdata_file.h"
#include "snapshotdata_buffer.h"
#include "sparse_array_1lv.h"
#include "rangevector.h"
#include "snapshotdata_memory.h"
#include "snapshotdata_common.h"
#include "snapshotdata_stretch.h"
#include "snapshotdata_blkinfo.h"
#include "veeamsnap_ioctl.h"


//#define SNAPSHOTDATA_BLK_SHIFT (COW_BLOCK_SIZE_DEGREE - SECTOR512_SHIFT)
#define SNAPSHOTDATA_BLK_SHIFT (sector_t)(PAGE_SHIFT - SECTOR512_SHIFT)
#define SNAPSHOTDATA_BLK_SIZE  (sector_t)(1 << SNAPSHOTDATA_BLK_SHIFT)
#define SNAPSHOTDATA_BLK_MASK  (sector_t)(SNAPSHOTDATA_BLK_SIZE-1)

typedef struct snapshotdata_s
{
	content_t content;
	dev_t dev_id;
	pid_t user_process_id;

	sparse_arr1lv_t  acc_map; //accordance map
	sparse_arr1lv_t  write_acc_map; // ???????????????

#ifdef SNAPDATA_ZEROED
	rangevector_t zero_sectors;
#endif

	volatile bool corrupted;
	atomic_t corrupted_cnt;

	int err_code;

	union {
		snapshotdata_shared_t* shared;
		snapshotdata_memory_t*  mem;
		snapshotdata_common_t* common;
		snapshotdata_stretch_t* stretch;
	};

	atomic64_t state_sectors_write;
}snapshotdata_t;



int snapshotdata_Init( void );
int snapshotdata_Done( void );

int snapshotdata_Destroy( snapshotdata_t* snapshotdata );
int snapshotdata_shared_cleanup( veeam_uuid_t* id );
int snapshotdata_FindByDevId( dev_t dev_id, snapshotdata_t** psnapshotdata );

int snapshotdata_Errno( dev_t dev_id, int* p_err_code );

void snapshotdata_order_border( const sector_t range_start_sect, const sector_t range_cnt_sect, sector_t *out_range_start_sect, sector_t *out_range_cnt_sect );


int _snapshotdata_test_block( sparse_arr1lv_t* to_acc_map, sector_t blk_ofs, bool* p_in_snapshot );
static inline int snapshotdata_TestBlock( snapshotdata_t* snapshotdata, sector_t blk_ofs, bool* p_in_snapshot )
{
	return _snapshotdata_test_block( &snapshotdata->acc_map, blk_ofs, p_in_snapshot );
};

int snapshotdata_TestBlockInBothMap( snapshotdata_t* snapshotdata, sector_t blk_ofs, bool* p_in_snapshot );

void snapshotdata_SetCorrupted( snapshotdata_t* snapshotdata, int err_code );
bool snapshotdata_IsCorrupted( snapshotdata_t* snapshotdata, dev_t original_dev_id );

//int _snapshotdata_write_to( snapshotdata_t* p_snapshotdata, sparse_arr1lv_t* to_map, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt );
//static inline int snapshotdata_write_to_snapshot( snapshotdata_t* p_snapshotdata, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
//{
//	return _snapshotdata_write_to( p_snapshotdata, &p_snapshotdata->acc_map, arr_ofs, arr, blk_ofs, blk_cnt );
//};

int _snapshotdata_write_dio_request_to( snapshotdata_t* snapshotdata, sparse_arr1lv_t* to_map, blk_deferred_request_t* dio_req );
static inline int snapshotdata_write_dio_request_to_snapshot( snapshotdata_t* snapshotdata, blk_deferred_request_t* dio_req )
{
	return _snapshotdata_write_dio_request_to( snapshotdata, &snapshotdata->acc_map, dio_req );
}

int snapshotdata_read_dio( snapshotdata_t* snapshotdata, blk_deferred_request_t* dio_req, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count, page_array_t* arr );
int snapshotdata_read_part( snapshotdata_t* snapshotdata, blk_redirect_bio_endio_t* rq_endio, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count );

int snapshotdata_write_to_image( snapshotdata_t* snapshotdata, struct bio* bio, struct block_device* defer_io_blkdev );

int snapshotdata_add_dev( snapshotdata_shared_t* shared, dev_t dev_id );

//int snapshotdata_add_range( snapshotdata_file_t* file, snapshotdata_blkinfo_t* blk_info, range_t* range );
int snapshotdata_add_ranges( snapshotdata_file_t* file, snapshotdata_blkinfo_t* blkinfo, struct ioctl_range_s* ranges, unsigned int ranges_length );

void snapshotdata_print_state( snapshotdata_t* snapshotdata );


#endif //SNAPSTORE
