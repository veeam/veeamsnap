// Copyright (c) Veeam Software Group GmbH

#pragma once
#include "container_spinlocking.h"
#include "tracker_queue.h"
#include "cbt_map.h"
#include "defer_io.h"
#include "veeamsnap_ioctl.h"
#include "snapshot.h"

typedef struct tracker_s
{
    content_sl_t content;
    dev_t original_dev_id;

    struct block_device* target_dev;
    tracker_disk_t* tr_disk;

    cbt_map_t* cbt_map;

    atomic_t is_captured;

    bool is_unfreezable; // when device have not filesystem and can not be freeze
    struct rw_semaphore unfreezable_lock; //locking io processing for unfreezable devices

    defer_io_t* defer_io;
    spinlock_t defer_io_lock;

    volatile unsigned long long snapshot_id;          // current snapshot for this device
}tracker_t;

int tracker_init( void );
int tracker_done( void );

#ifdef VEEAMSNAP_BDEV_BIO
int tracker_find_by_bdev(struct block_device *bdev, tracker_t** ptracker);
#else
int tracker_find_by_queue_and_sector(tracker_disk_t* queue, sector_t sector, tracker_t** ptracker);
#endif
int tracker_find_intersection(tracker_disk_t* queue, sector_t b1, sector_t e1, tracker_t** ptracker);

int tracker_find_by_dev_id(dev_t dev_id, tracker_t** ptracker);
//int tracker_find_by_sb(struct super_block* sb, tracker_t** ptracker);

int tracker_enum_cbt_info(int max_count, struct cbt_info_s* p_cbt_info, int* p_count);

int tracker_capture_snapshot( snapshot_t* p_snapshot );
int tracker_release_snapshot( snapshot_t* p_snapshot );

int tracker_create(unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree, cbt_map_t* cbt_map, tracker_t** ptracker);
int tracker_remove( tracker_t* tracker );
int tracker_remove_all( void );

void tracker_cbt_bitmap_set( tracker_t* tracker, sector_t sector, sector_t sector_cnt );

bool tracker_cbt_bitmap_lock( tracker_t* tracker );
void tracker_cbt_bitmap_unlock( tracker_t* tracker );

void tracker_print_state( void );
