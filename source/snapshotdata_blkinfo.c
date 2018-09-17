#include "stdafx.h"
#include "snapshotdata_blkinfo.h"

#ifndef SNAPSTORE

void snapshotdata_blkinfo_create( snapshotdata_blkinfo_t* blkinfo, sector_t cnt )
{
	spin_lock_init( &blkinfo->locker );
	blkinfo->limit = cnt;
	blkinfo->pos = 0;

	log_traceln_sect( "blocks limit =", blkinfo->limit );
}

void snapshotdata_blkinfo_add( snapshotdata_blkinfo_t* blkinfo, sector_t cnt )
{
	spin_lock( &blkinfo->locker );
	blkinfo->limit += cnt;
	spin_unlock( &blkinfo->locker );
}

int snapshotdata_blkinfo_pos_increment( snapshotdata_blkinfo_t* blkinfo, sector_t blk_cnt, sector_t * p_blk_pos )
{
	int res = SUCCESS;

	spin_lock( &blkinfo->locker );
	if ((blkinfo->pos + blk_cnt) < blkinfo->limit){
		*p_blk_pos = blkinfo->pos;
		blkinfo->pos += blk_cnt;
	}
	else
		res = -EINVAL;
	spin_unlock( &blkinfo->locker );

	return res;
}

#endif //SNAPSTORE
