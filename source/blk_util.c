#include "stdafx.h"
#include "container.h"
#include "queue_spinlocking.h"
#include "blk_util.h"


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

void blk_dev_close( struct block_device* blk_dev )
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	blkdev_put( blk_dev );
#else
	blkdev_put( blk_dev, FMODE_READ );
#endif
}

int _blk_dev_get_info( struct block_device* blk_dev, blk_dev_info_t* pdev_info )
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

int blk_dev_get_info( dev_t dev_id, blk_dev_info_t* pdev_info )
{
	int result = SUCCESS;
	struct block_device* blk_dev;

	result = blk_dev_open( dev_id, &blk_dev );
	if (result != SUCCESS){
		log_errorln_dev_t( "Failed to open device=", dev_id );
		return result;
	}
	result = _blk_dev_get_info( blk_dev, pdev_info );
	if (result != SUCCESS){
		log_errorln_dev_t( "Cannot identify block device dev_id=0x", dev_id );
	}

	blk_dev_close( blk_dev );

	return result;
}


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
