// Copyright (c) Veeam Software Group GmbH

#pragma once
#include "tracker_queue.h"
#include "sparse_bitmap.h"
#include "rangelist.h"
#include "page_array.h"

typedef struct snapdata_collector_s
{
    struct list_head link;

    dev_t dev_id;
    struct block_device* device;

    tracker_disk_t* tr_disk;

    void* magic_buff;
    size_t magic_size;
#ifdef SNAPDATA_SPARSE_CHANGES
    sparse_bitmap_t changes_sparse;
#else
    page_array_t* changes;
    sector_t start_index;
    sector_t length;
#endif
    atomic64_t collected_size;
    atomic64_t already_set_size;

    int fail_code;

    struct mutex locker;
}snapdata_collector_t;


int snapdata_collect_Init( void );
void snapdata_collect_Done( void );

int snapdata_collect_LocationStart( dev_t dev_id, void* MagicUserBuff, size_t MagicLength );
int snapdata_collect_LocationGet( dev_t dev_id, rangelist_t* rangelist, size_t* ranges_count );
int snapdata_collect_LocationComplete( dev_t dev_id );

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int snapdata_collect_Find(struct bio *bio, snapdata_collector_t** p_collector);
#else
int snapdata_collect_Find(struct bio *bio, struct request_queue *queue, snapdata_collector_t** p_collector);
#endif

void snapdata_collect_Process( snapdata_collector_t* collector, struct bio *bio );

