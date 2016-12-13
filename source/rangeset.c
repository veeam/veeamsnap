#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#include "stdafx.h"
#include "range.h"
#include "rangeset.h"

int rangeset_create( size_t range_count, rangeset_t** p_rangeset )
{
	rangeset_t* rangeset = NULL;
	size_t size = sizeof( rangeset_t ) + sizeof( range_t )*range_count;
	rangeset = dbg_kzalloc( size, GFP_KERNEL );
	if (rangeset == NULL)
		return -ENOMEM;

	rangeset->range_count = range_count;
	*p_rangeset = rangeset;
	return SUCCESS;
}

void rangeset_destroy( rangeset_t* rangeset )
{
	if (NULL != rangeset){
		dbg_kfree( rangeset );
	}
}

int rangeset_set( rangeset_t* rangeset, size_t inx, range_t rg )
{
	if (inx >= rangeset->range_count)
		return -EINVAL;

	rangeset->ranges[inx].ofs = rg.ofs;
	rangeset->ranges[inx].cnt = rg.cnt;
	return SUCCESS;
}

int rangeset_v2p( rangeset_t* rangeset, sector_t virt_offset, sector_t virt_length, sector_t* p_phys_offset, sector_t* p_phys_length )
{
	size_t inx;
	sector_t virt_left = 0;
	for (inx = 0; inx < rangeset->range_count; ++inx){
		sector_t virt_right = virt_left + rangeset->ranges[inx].cnt;

		if ((virt_offset >= virt_left) && (virt_offset < virt_right)){
			*p_phys_offset = rangeset->ranges[inx].ofs + (virt_offset - virt_left);
			*p_phys_length = min( virt_length, virt_right - virt_offset );

			return SUCCESS;
		}
		virt_left = virt_right;
	}
	return -ENODATA;
}


sector_t rangeset_length( rangeset_t* rangeset )
{
	size_t inx;
	sector_t cnt = 0;
	for (inx = 0; inx < rangeset->range_count; ++inx){
		cnt += rangeset->ranges[inx].cnt;
	}
	return cnt;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */