#pragma once

#ifndef SNAPSHOTDATA_H
#define SNAPSHOTDATA_H

#include "dio_request.h"

//////////////////////////////////////////////////////////////////////////
#define SNAPSHOTDATA_TYPE_INVALID 0
#define SNAPSHOTDATA_TYPE_MEM  1
#define SNAPSHOTDATA_TYPE_DISK 2
#define SNAPSHOTDATA_TYPE_COMMON  3
#define SNAPSHOTDATA_TYPE_STRETCH 4

//snapshot data block size is equal to PAGE_SIZE
#define SNAPSHOTDATA_BLK_SHIFT (PAGE_SHIFT - SECTOR512_SHIFT)
#define SNAPSHOTDATA_BLK_SIZE  (1 << SNAPSHOTDATA_BLK_SHIFT)
#define SNAPSHOTDATA_BLK_MASK  (sector_t)(SNAPSHOTDATA_BLK_SIZE-1)

static inline void snapshotdata_order_border( const sector_t range_start_sect, const sector_t range_cnt_sect, sector_t *out_range_start_sect, sector_t *out_range_cnt_sect )
{
	sector_t unorder_ofs;
	sector_t unorder_len;

	unorder_ofs = range_start_sect & SNAPSHOTDATA_BLK_MASK;
	*out_range_start_sect = range_start_sect - unorder_ofs;

	unorder_len = (range_cnt_sect + unorder_ofs) & SNAPSHOTDATA_BLK_MASK;
	if (unorder_len == 0)
		*out_range_cnt_sect = (range_cnt_sect + unorder_ofs);
	else
		*out_range_cnt_sect = (range_cnt_sect + unorder_ofs) + (SNAPSHOTDATA_BLK_SIZE - unorder_len);
};

//////////////////////////////////////////////////////////////////////////
typedef struct snapshotdata_blk_info_s{
	spinlock_t locker;
	sector_t pos;
	sector_t cnt;
}snapshotdata_blk_info_t;

typedef struct snapshotdata_mem_s{
	snapshotdata_blk_info_t mem_blk_info;

	void*  buff;
	size_t buff_size;
}snapshotdata_mem_t;

typedef struct snapshotdata_disk_s{
	snapshotdata_blk_info_t dsk_blk_info;
	dev_t dsk_blk_dev_id;
	struct block_device*  dsk_blk_dev;
	rangeset_t* dsk_datarangeset;  //for converting virtual snapshot blocks numbers to real snapshot blocks
}snapshotdata_disk_t;

typedef struct snapshotdata_common_disk_s{
	content_t content;
	shared_resource_t sharing_header;

	unsigned int unique_id;

	snapshotdata_blk_info_t cmn_blk_info;
	dev_t cmn_blk_dev_id;
	struct block_device*  cmn_blk_dev;
	rangeset_t* cmn_datarangeset;  //for converting virtual snapshot blocks numbers to real snapshot blocks

}snapshotdata_common_disk_t;

typedef struct snapshotdata_stretch_s{
	content_t content;
	shared_resource_t sharing_header;

	unsigned char unique_id[16];

	ctrl_pipe_t* ctrl_pipe;

	snapshotdata_blk_info_t stretch_blk_info;
	dev_t stretch_blk_dev_id;
	struct block_device*  stretch_blk_dev;
	rangevector_t stretch_dataranges;   //for converting virtual snapshot blocks numbers to real snapshot blocks

	sector_t stretch_empty_limit;

	volatile bool halffilled;
	volatile bool overflowed;
}snapshotdata_stretch_disk_t;

//////////////////////////////////////////////////////////////////////////
static inline snapshotdata_common_disk_t* snapshotdata_common_disk_get_resource( snapshotdata_common_disk_t* comm_disk )
{
	return (snapshotdata_common_disk_t*)shared_resource_get( &comm_disk->sharing_header );
}

static inline void snapshotdata_common_disk_put_resource( snapshotdata_common_disk_t* comm_disk )
{
	shared_resource_put( &comm_disk->sharing_header );
}
//////////////////////////////////////////////////////////////////////////
static inline snapshotdata_stretch_disk_t* snapshotdata_stretch_disk_get_resource( snapshotdata_stretch_disk_t* stretch_disk )
{
	return (snapshotdata_stretch_disk_t*)shared_resource_get( &stretch_disk->sharing_header );
}

static inline void snapshotdata_stretch_disk_put_resource( snapshotdata_stretch_disk_t* stretch_disk )
{
	shared_resource_put( &stretch_disk->sharing_header );
}
//////////////////////////////////////////////////////////////////////////
typedef struct snapshotdata_s
{
	content_t content;
	dev_t dev_id;
	pid_t user_process_id;

	sparse_arr1lv_t  accordance_map;
	sparse_arr1lv_t  write_acc_map;

#ifdef SNAPDATA_ZEROED
	rangevector_t zero_sectors;
#endif

	volatile bool corrupted;
	atomic_t corrupted_cnt;

	int err_code;
	char type;
	union {
		snapshotdata_mem_t*  mem;
		snapshotdata_disk_t* disk;
		snapshotdata_common_disk_t* common;
		snapshotdata_stretch_disk_t* stretch;
	};

	atomic64_t state_sectors_write;
}snapshotdata_t;
//////////////////////////////////////////////////////////////////////////

int snapshotdata_Init( void );
int snapshotdata_Done( void );

int snapshotdata_DeleteByDevId( dev_t dev_id );
int snapshotdata_DeleteAll( void );

int snapshotdata_AddDiskInfo( dev_t dev_id, dev_t dev_id_host_data, rangeset_t* rangeset );

int snapshotdata_AddMemInfo( dev_t dev_id, size_t size );

int snapshotdata_CleanInfo( dev_t dev_id );

int snapshotdata_FindByDevId( dev_t dev_id, snapshotdata_t** pp_snapshotdata );

int snapshotdata_Errno( dev_t dev_id, int* p_err_code );
//////////////////////////////////////////////////////////////////////////

int snapshotdata_CollectLocationStart( dev_t dev_id, void* MagicUserBuff, size_t MagicLength );

int snapshotdata_CollectLocationGet( dev_t dev_id, rangeset_t** p_rangeset );

//////////////////////////////////////////////////////////////////////////

void snapshotdata_Reset( snapshotdata_t* p_snapshotdata );

int __snapshotdata_test_block( sparse_arr1lv_t* to_acc_map, sector_t blk_ofs, bool* p_in_snapshot );

static inline int snapshotdata_TestBlock( snapshotdata_t* p_snapshotdata, sector_t blk_ofs, bool* p_in_snapshot )
{
	return __snapshotdata_test_block( &p_snapshotdata->accordance_map, blk_ofs, p_in_snapshot );
};

int snapshotdata_TestBlockInBothMap( snapshotdata_t* p_snapshotdata, sector_t blk_ofs, bool* p_in_snapshot );

void snapshotdata_SetCorrupted( snapshotdata_t* p_snapshotdata, int err_code );
bool snapshotdata_IsCorrupted( snapshotdata_t* p_snapshotdata, dev_t original_dev_id );

int __snapshotdata_write_to( snapshotdata_t* p_snapshotdata, sparse_arr1lv_t* to_map, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt );

static inline int snapshotdata_write_to_snapshot( snapshotdata_t* p_snapshotdata, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	return __snapshotdata_write_to( p_snapshotdata, &p_snapshotdata->accordance_map, arr_ofs, arr, blk_ofs, blk_cnt );
};

int __snapshotdata_write_dio_request_to( snapshotdata_t* p_snapshotdata, sparse_arr1lv_t* to_map, dio_request_t* dio_req );

static inline int snapshotdata_write_dio_request_to_snapshot( snapshotdata_t* p_snapshotdata, dio_request_t* dio_req )
{
	return __snapshotdata_write_dio_request_to( p_snapshotdata, &p_snapshotdata->accordance_map, dio_req );
}

int snapshotdata_read_dio_request_from_snapshot( snapshotdata_t* p_snapshotdata, dio_request_t* dio_req );
int snapshotdata_read_dio( snapshotdata_t* p_snapshotdata, dio_request_t* dio_req, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count, page_array_t* arr );
int snapshotdata_read_part( snapshotdata_t* p_snapshotdata, redirect_bio_endio_t* rq_endio, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count );

int snapshotdata_write_to_image( snapshotdata_t* snapshotdata, struct bio* bio, struct block_device* defer_io_blkdev );


//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_Init( void );
int snapshotdata_common_Done( void );

int snapshotdata_common_reserve( unsigned int* p_common_file_id );
int snapshotdata_common_unreserve( unsigned int common_file_id );
int snapshotdata_common_create( unsigned int common_file_id, rangeset_t* datarangeset, dev_t dev_id );
int snapshotdata_common_add_dev( unsigned int common_file_id, dev_t dev_id );
//////////////////////////////////////////////////////////////////////////

int snapshotdata_stretch_Init( void );
int snapshotdata_stretch_Done( void );

void snapshotdata_stretch_free( snapshotdata_stretch_disk_t* stretch_disk );
snapshotdata_stretch_disk_t* snapshotdata_stretch_create( unsigned char id[16], dev_t dev_id );
int snapshotdata_stretch_add_dev( snapshotdata_stretch_disk_t* stretch_disk, dev_t dev_id );

snapshotdata_stretch_disk_t* snapshotdata_stretch_get( unsigned char id[16] );
int snapshotdata_stretch_add_range( snapshotdata_stretch_disk_t* stretch_disk, range_t* range );

void snapshotdata_stretch_halffill( snapshotdata_stretch_disk_t* stretch_disk, ssize_t fill_status );
void snapshotdata_stretch_overflow( snapshotdata_stretch_disk_t* stretch_disk, unsigned int error_code );
void snapshotdata_stretch_terminate( snapshotdata_stretch_disk_t* stretch_disk );

//////////////////////////////////////////////////////////////////////////
void snapshotdata_print_state( snapshotdata_t* snapshotdata );
//////////////////////////////////////////////////////////////////////////
#endif //SNAPSHOTDATA_H