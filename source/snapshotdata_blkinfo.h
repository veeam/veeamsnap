#pragma once

#ifndef SNAPSTORE

typedef struct snapshotdata_blkinfo_s{
	spinlock_t locker;
	sector_t pos; //current filled
	sector_t limit; //capacity
}snapshotdata_blkinfo_t;

void snapshotdata_blkinfo_create( snapshotdata_blkinfo_t* blkinfo, sector_t cnt );

void snapshotdata_blkinfo_add( snapshotdata_blkinfo_t* blkinfo, sector_t cnt );

int snapshotdata_blkinfo_pos_increment( snapshotdata_blkinfo_t* blkinfo, sector_t blk_cnt, sector_t * p_blk_pos );

#endif //SNAPSTORE
