#include "stdafx.h"
#include "container.h"
#include "container_spinlocking.h"
#include "range.h"
#include "rangeset.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"
#include "snapshot.h"
#include "shared_resource.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"
#include "log.h"

//////////////////////////////////////////////////////////////////////////
static container_t SnapshotDatas;
static unsigned long long common_file_id_map;

static container_t SnapshotCommonDisks;
static container_t SnapshotStretchDisks;

int __snapshotdata_create_common( snapshotdata_t* p_snapshotdata, sector_t info_dev_count_sect );
int __snapshotdata_create_in_disk( dev_t orig_dev_id, dev_t snapshot_dev_id, rangeset_t* rangeset, snapshotdata_t** pp_snapshotdata );
int __snapshotdata_create_in_memory( dev_t orig_dev_id, stream_size_t snapshotdatasize, snapshotdata_t** pp_snapshotdata );
int __snapshotdata_destroy( snapshotdata_t* p_snapshotdata );

int __snapshotdata_check_io_compatibility( dev_t dev_id, struct block_device* snapdata_blk_dev, bool* is_compatibility );
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_reserve( unsigned int* p_common_file_id )
{
	unsigned int common_file_id;

	//find first free bit
	for (common_file_id = 0; common_file_id < sizeof( unsigned long long ) * 8; ++common_file_id){
		if (0 == (((unsigned long long)1 << common_file_id) & common_file_id_map)){
			*p_common_file_id = common_file_id;
			common_file_id_map |= ((unsigned long long)1 << common_file_id);
			log_traceln_d( "Reserved. id=", common_file_id );
			return SUCCESS;
		}
	}

	return -EBUSY;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_unreserve( unsigned int common_file_id )
{
	if (common_file_id >= (sizeof( unsigned long long ) * 8)){
		return -EINVAL;
	}
	if (((unsigned long long)1 << common_file_id) & common_file_id_map){
		common_file_id_map &= ~((unsigned long long)1 << common_file_id);
		log_traceln_d( "Unreserved. id=", common_file_id );
	}else{
		log_errorln_d( "Already unreserved. id=", common_file_id );
	}

	return SUCCESS;

}
//////////////////////////////////////////////////////////////////////////
void __snapshotdata_create_blk_info( snapshotdata_blk_info_t* blk_info, sector_t blk_cnt )
{
	spin_lock_init( &blk_info->locker );
	blk_info->cnt = blk_cnt;
	blk_info->pos = 0;

	log_traceln_sect( "blocks count =", blk_info->cnt );
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_Init( void )
{
	return container_init( &SnapshotCommonDisks, sizeof( snapshotdata_common_disk_t ), NULL/*"vsnap_SnapshotCommonDisks"*/ );
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_Done( void )
{
	content_t* content;
	snapshotdata_common_disk_t* comm_disk;
	CONTAINER_FOREACH_BEGIN( SnapshotCommonDisks, content )
	{
		comm_disk = (snapshotdata_common_disk_t*)content;
		log_errorln_d( "Still in use. id=", comm_disk->unique_id );
		log_errorln_dev_t( "device=", comm_disk->cmn_blk_dev_id );
		log_errorln_d( "owners count=", atomic_read( &comm_disk->sharing_header.own_cnt) );

	}CONTAINER_FOREACH_END( SnapshotCommonDisks );

	return container_done( &SnapshotCommonDisks );
}
//////////////////////////////////////////////////////////////////////////
void snapshotdata_common_free_cb( void* this_resource )
{
	snapshotdata_common_disk_t* comm_disk = (snapshotdata_common_disk_t*)this_resource;
	log_traceln_d( "common_file_id=", comm_disk->unique_id );

	if (comm_disk->cmn_datarangeset != NULL){
		rangeset_destroy( comm_disk->cmn_datarangeset );
		comm_disk->cmn_datarangeset = NULL;
	}

	if (comm_disk->cmn_blk_dev != NULL){
		blk_dev_close( comm_disk->cmn_blk_dev );
		comm_disk->cmn_blk_dev = NULL;
	}

	snapshotdata_common_unreserve( comm_disk->unique_id );

	container_free( &comm_disk->content );
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_create( unsigned int common_file_id, rangeset_t* rangeset, dev_t dev_id )
{
	int res = SUCCESS;
	snapshotdata_common_disk_t* comm_disk = NULL;

	log_traceln_d( "common_file_id=", common_file_id );
	log_traceln_dev_t( "dev_id=", dev_id );

	comm_disk = (snapshotdata_common_disk_t*)content_new( &SnapshotCommonDisks );
	if (comm_disk == NULL)
		return -ENOMEM;

	do{
		res = blk_dev_open( dev_id, &comm_disk->cmn_blk_dev );
		if (res != SUCCESS){
			log_errorln( "Failed to open snapshot device." );
			break;
		}

		shared_resource_init( &comm_disk->sharing_header, comm_disk, snapshotdata_common_free_cb );
		__snapshotdata_create_blk_info( &comm_disk->cmn_blk_info, rangeset_length( rangeset ) );

		comm_disk->cmn_datarangeset = rangeset;
		comm_disk->cmn_blk_dev_id = dev_id;
		comm_disk->unique_id = common_file_id;

		container_push_back( &SnapshotCommonDisks, &comm_disk->content );

		log_traceln( "New snapshot common disk added to container." );

	} while (false);

	if (res != SUCCESS){
		if (comm_disk->cmn_blk_dev != NULL){
			blk_dev_close( comm_disk->cmn_blk_dev );
			comm_disk->cmn_blk_dev = NULL;
		}
		content_free( &comm_disk->content );
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_add_dev( unsigned int common_file_id, dev_t dev_id )
{
	int res = SUCCESS;
	bool is_compatible = false;
	blk_dev_info_t info_dev;
	snapshotdata_t* snapdata = NULL;
	snapshotdata_common_disk_t* comm_disk = NULL;
	snapshotdata_common_disk_t* curr_comm_disk;
	content_t* content;
	log_traceln_dev_t( " device=", dev_id );

	CONTAINER_FOREACH_BEGIN( SnapshotCommonDisks, content ){
		curr_comm_disk = (snapshotdata_common_disk_t*)content;
		if (curr_comm_disk->unique_id == common_file_id){
			comm_disk = curr_comm_disk;
			break;
		}
	}CONTAINER_FOREACH_END( SnapshotCommonDisks );

	if (comm_disk == NULL){
		log_errorln_d( "Cannot find common file ", common_file_id );
		return -ENODEV;
	}

	//check snapshotdata io limit compatibility
	res = __snapshotdata_check_io_compatibility( dev_id, comm_disk->cmn_blk_dev, &is_compatible );
	if (res != SUCCESS){
		return res;
	}
	if (!is_compatible){
		log_errorln_dev_t( "Incompatible physical block size for device ", dev_id );
		return -EPERM;
	}

	do{
		snapdata = (snapshotdata_t*)content_new( &SnapshotDatas );
		if (snapdata == NULL){
			res = -ENOMEM;
			break;
		}

		snapdata->dev_id = dev_id;
		res = blk_dev_get_info( snapdata->dev_id, &info_dev );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot obtain info about original device=.", dev_id );
			break;
		}

		res = __snapshotdata_create_common( snapdata, info_dev.count_sect );
		if (res != SUCCESS){
			break;
		}
	} while (false);
	if (res == SUCCESS){
		snapdata->type = SNAPSHOTDATA_TYPE_COMMON;
		snapdata->common = snapshotdata_common_disk_get_resource( comm_disk );

#ifdef SNAPDATA_ZEROED
		if (get_zerosnapdata( )){
			if (snapdata->dev_id == comm_disk->cmn_blk_dev_id){// snapshot data on same partition
				size_t range_inx = 0;
				rangeset_t* snaprange = comm_disk->cmn_datarangeset;

				log_traceln_dev_t( "Zeroing ranges set for device ", snapdata->dev_id );
				log_traceln_sz( "Zeroing ranges count ", snaprange->range_count );
				for (range_inx = 0; range_inx < snaprange->range_count; ++range_inx){
					res = rangevector_add( &snapdata->zero_sectors, snaprange->ranges[range_inx] );
					if (res != SUCCESS)
						break;
					log_traceln_range( "Zeroing range:", snaprange->ranges[range_inx] );
				}
				if (res != SUCCESS){
					log_errorln_d( "Failed to set zero sectors. errno=", 0 - res );
				}
				else{
					rangevector_sort( &snapdata->zero_sectors );
				}
			}
			else{
				log_traceln_dev_t( "Haven`t zeroing ranges.", snapdata->dev_id );
			}
		}
#endif
		container_push_back( &SnapshotDatas, &snapdata->content );
	}
	else{
		if (snapdata != NULL){
			content_free( &snapdata->content );
		}
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_Init( void )
{
	return container_init( &SnapshotDatas, sizeof( snapshotdata_t ), NULL/*"vsnap_SnapshotDatas"*/ );
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_Done( void )
{
	int result;

	result = snapshotdata_DeleteAll( );

	if (result == SUCCESS)
		result = container_done( &SnapshotDatas );

	return result;
}

//////////////////////////////////////////////////////////////////////////
int snapshotdata_DeleteByDevId( dev_t dev_id )
{
	int result = -ENODATA;
	content_t* content;
	snapshotdata_t* snapshotdata = NULL;
	log_traceln_dev_t( "Removing snapshot data for device=", dev_id );

	CONTAINER_FOREACH_BEGIN( SnapshotDatas, content )
	{
		if (dev_id == ((snapshotdata_t*)content)->dev_id){
			snapshotdata = (snapshotdata_t*)content;
			__container_del( &SnapshotDatas, content );// removing from list with access lock
			break;
		}
	}
	CONTAINER_FOREACH_END( SnapshotDatas );

	if (snapshotdata != NULL){//calling destructors without list lock
		result = __snapshotdata_destroy( snapshotdata );
		content_free( &snapshotdata->content );
	}
	return result;
}
//////////////////////////////////////////////////////////////////////////
int _CleanuAll_cb( content_t* pCnt, void* param )
{
	snapshotdata_t* p_snapshotdata = (snapshotdata_t*)pCnt;

	__snapshotdata_destroy( p_snapshotdata );

	return ENODATA;	//continue
}

int snapshotdata_DeleteAll( void )
{
	int result = SUCCESS;
	int status = SUCCESS;

	log_traceln( "Removing all snapshot info data." );

	if (SUCCESS == result){
		if (SUCCESS == status){
			container_enum_and_free( &SnapshotDatas, _CleanuAll_cb, NULL );
		}
		else{
			log_traceln_d( "Some snapshot info data isn`t removed. status=", (0 - status) );
		}
		result = status;
	}
	else{
		log_traceln_d( "Failed. result =", (0 - result) );
	}

	return result;
}

int snapshotdata_AddDiskInfo( dev_t dev_id, dev_t dev_id_host_data, rangeset_t* rangeset )
{
	int res = SUCCESS;
	snapshotdata_t* sdata;

	log_traceln_dev_t( "device: ", dev_id );

	log_traceln_sz( "rangeset count=", rangeset->range_count );

	res = snapshotdata_FindByDevId( dev_id, &sdata );
	if (res == SUCCESS){
		bool changed = false;

		log_traceln_dev_t( "Snapshot data already exist for device=", dev_id );

		if (sdata->type != SNAPSHOTDATA_TYPE_DISK)
			changed = true;
		else{

			if (sdata->disk == NULL)
				return -EINVAL;

			if (sdata->disk->dsk_datarangeset->range_count != rangeset->range_count){
				changed = true;
				log_traceln( "range_count is not equal." );
			}
			else{
				size_t inx = 0;
				for (; inx < sdata->disk->dsk_datarangeset->range_count; ++inx){
					if ((sdata->disk->dsk_datarangeset->ranges[inx].ofs != rangeset->ranges[inx].ofs) ||
						(sdata->disk->dsk_datarangeset->ranges[inx].cnt != rangeset->ranges[inx].cnt))
					{
						changed = true;
						log_traceln( "offset or block count is not equal." );
						break;
					}
				}
			}
		}

		if (!changed)
			return -EALREADY;

		res = snapshotdata_DeleteByDevId( dev_id );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot delete snapshot data info for device=", dev_id );
			return res;
		}
	}
	else if (res != -ENODATA){
		log_errorln_dev_t( "Failed to find snapshot data for device=", dev_id );
		return res;
	}
	res = __snapshotdata_create_in_disk( dev_id, dev_id_host_data, rangeset, &sdata );
	if (res != SUCCESS){
		log_errorln_dev_t( "Cannot set snapshot data info for device=", dev_id );
	}
	return res;
}

//////////////////////////////////////////////////////////////////////////
int snapshotdata_AddMemInfo( dev_t dev_id, size_t size )
{
	int res = SUCCESS;
	snapshotdata_t* sdata;

	res = snapshotdata_FindByDevId( dev_id, &sdata );
	if (res == SUCCESS){
		log_traceln_dev_t( "Snapshot data already exist for device=", dev_id );

		if ((sdata->type == SNAPSHOTDATA_TYPE_MEM) && (sdata->mem->buff_size == size))
			return -EALREADY;

		res = snapshotdata_DeleteByDevId( dev_id );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot delete snapshot data info for device=", dev_id );
			return res;
		}
	}
	if (res != -ENODATA){
		log_errorln_dev_t( "Failed to find snapshot data for device=", dev_id );
		return res;
	}

	res = __snapshotdata_create_in_memory( dev_id, size, &sdata );
	if (res != SUCCESS){
		log_errorln_dev_t( "Cannot set snapshot data info for device=", dev_id );
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////

int snapshotdata_CleanInfo( dev_t dev_id )
{
	int res = snapshotdata_DeleteByDevId( dev_id );
	if (res == -ENODATA){
		log_traceln_dev_t( "Snapshot data already absent for device=", dev_id );
		return -EALREADY;
	}
	else if (res != SUCCESS){
		log_errorln_dev_t( "Failed to delete snapshot data for device=", dev_id );
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
typedef struct _FindByDevId_cb_s
{
	dev_t dev_id;
	snapshotdata_t* p_snapshotdata;
}_FindByDevId_cb_t;

int _FindByDevId_cb( content_t* pCnt, void* param )
{
	snapshotdata_t* p_snapshotdata = (snapshotdata_t*)pCnt;
	_FindByDevId_cb_t* p_param = (_FindByDevId_cb_t*)param;

	if (p_param->dev_id == p_snapshotdata->dev_id){
		p_param->p_snapshotdata = p_snapshotdata;
		return SUCCESS;
	}

	return ENODATA;	//continue
}

int snapshotdata_FindByDevId( dev_t dev_id, snapshotdata_t** pp_snapshotdata )
{
	int result = SUCCESS;
	_FindByDevId_cb_t param;
	param.dev_id = dev_id;
	param.p_snapshotdata = NULL;

	result = container_enum( &SnapshotDatas, _FindByDevId_cb, &param );

	if (param.p_snapshotdata != NULL){
		if (pp_snapshotdata != NULL){
			*pp_snapshotdata = param.p_snapshotdata;
			log_traceln_dev_t( "Snapshot data for device successfully found. dev_id=", dev_id );
		}
	}
	else{
		result = -ENODATA;
		log_traceln_dev_t( "Snapshot data for device not found. dev_id=", dev_id );
	}
	return result;
}


int snapshotdata_Errno( dev_t dev_id, int* p_err_code )
{
	int res;
	snapshotdata_t* snapshotdata = NULL;

	res = snapshotdata_FindByDevId( dev_id, &snapshotdata );
	if (res != SUCCESS)
		return res;

	*p_err_code = snapshotdata->err_code;
	return SUCCESS;
}


void snapshotdata_Reset( snapshotdata_t* p_snapshotdata )
{
	log_traceln(".");
	atomic_set( &p_snapshotdata->corrupted_cnt, 0 );
	p_snapshotdata->corrupted = false;


	if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_DISK){
		p_snapshotdata->disk->dsk_blk_info.pos = 0;
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_MEM){
		p_snapshotdata->mem->mem_blk_info.pos = 0;
		memset( p_snapshotdata->mem->buff, 0, p_snapshotdata->mem->buff_size );
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_COMMON){
		// do nothing
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		// do nothing
	}

	sparse_array_1lv_reset( &p_snapshotdata->accordance_map );
	sparse_array_1lv_reset( &p_snapshotdata->write_acc_map );

#ifdef SNAPDATA_ZEROED
	if (get_zerosnapdata( )){
	rangevector_cleanup( &p_snapshotdata->zero_sectors );
	log_traceln( "Zero sectors bitmap cleanup." );
	}
#endif
}

//////////////////////////////////////////////////////////////////////////
int __snapshotdata_create_common( snapshotdata_t* p_snapshotdata, sector_t info_dev_count_sect )
{
	int res;
	index_t acc_map_size;

	acc_map_size = (index_t)(info_dev_count_sect >> SNAPSHOTDATA_BLK_SHIFT);
	if ((info_dev_count_sect & SNAPSHOTDATA_BLK_MASK))
		acc_map_size += 1;

	res = sparse_array_1lv_init( &p_snapshotdata->accordance_map, 0, acc_map_size );
	if (res != SUCCESS){
		log_errorln( "Failed to initialize sparse array accordance_map." );
		return res;
	}

	res = sparse_array_1lv_init( &p_snapshotdata->write_acc_map, 0, acc_map_size );
	if (res != SUCCESS){
		log_errorln( "Failed to initialize sparse array write_acc_map." );
		return res;
	}

#ifdef SNAPDATA_ZEROED
	if (get_zerosnapdata( )){
	log_traceln_sect( "Zero ranges bitmap create. sectors count=", info_dev_count_sect );
	rangevector_create( &p_snapshotdata->zero_sectors, true );
	}
#endif

	atomic64_set( &p_snapshotdata->state_sectors_write, 0 );

	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_create_in_disk( dev_t orig_dev_id, dev_t snapshot_dev_id, rangeset_t* rangeset, snapshotdata_t** pp_snapshotdata )
{
	int res = SUCCESS;
	blk_dev_info_t info_dev = { 0 };
	snapshotdata_t* snapshotdata;

	log_traceln( "Creating on disk." );
	log_traceln_dev_t( "Original device: ", orig_dev_id );
	log_traceln_dev_t( "Snapshot device: ", snapshot_dev_id );

	snapshotdata = (snapshotdata_t*)content_new( &SnapshotDatas );
	if (snapshotdata == NULL){
		log_errorln( "Cannot allocate snapshot data struct." );
		return -ENOMEM;
	}

	log_traceln_p( "snapshotdata=", snapshotdata );

	snapshotdata->user_process_id = task_pid_nr( current );
	log_traceln_d( "current task id=", snapshotdata->user_process_id );

	snapshotdata->err_code = SUCCESS;
	snapshotdata->type = SNAPSHOTDATA_TYPE_DISK;
	do{
		snapshotdata_disk_t* snapdisk;
		snapshotdata->dev_id = orig_dev_id;
		snapshotdata->disk = snapdisk = dbg_kzalloc( sizeof( snapshotdata_disk_t ), GFP_KERNEL );
		if (snapdisk == NULL){
			res = -ENOMEM;
			break;
		}

		res = blk_dev_get_info( snapshotdata->dev_id, &info_dev );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot obtain info about original device=.", snapshotdata->dev_id );
			break;
		}

		res = blk_dev_open( snapshot_dev_id, &snapdisk->dsk_blk_dev );
		if (res != SUCCESS){
			log_errorln( "Failed to open snapshot device." );
			break;
		}
		snapdisk->dsk_blk_dev_id = snapshot_dev_id;

		snapdisk->dsk_datarangeset = rangeset;
		__snapshotdata_create_blk_info( &snapdisk->dsk_blk_info, rangeset_length( rangeset ) );

		res = __snapshotdata_create_common( snapshotdata, info_dev.count_sect );
		if (res != SUCCESS){
			break;
		}
#ifdef SNAPDATA_ZEROED
		if (get_zerosnapdata( )){
			if (snapshotdata->dev_id == snapdisk->dsk_blk_dev_id){//if snapshot data on device under snapshot, then set zero sectors
				size_t range_inx = 0;
				rangeset_t* snaprange = snapdisk->dsk_datarangeset;

				log_traceln_dev_t( "Zeroing ranges set for device ", snapshotdata->dev_id );
				for (range_inx = 0; range_inx < snaprange->range_count; ++range_inx){
					res = rangevector_add( &snapshotdata->zero_sectors, snaprange->ranges[range_inx] );
					if (res != SUCCESS)
						break;

					log_traceln_range( "Zeroing range:", snaprange->ranges[range_inx] );
				}
				if (res != SUCCESS){
					log_errorln_d( "Failed to set zero sectors. errno=", 0 - res );
					break;
				}
				rangevector_sort( &snapshotdata->zero_sectors );
			}
			else{
				log_traceln_dev_t( "Haven`t zeroing ranges.", snapshotdata->dev_id );
			}
		}
#endif
		snapshotdata_Reset( snapshotdata );
	} while (false);

	if (res == SUCCESS){
		*pp_snapshotdata = snapshotdata;
		container_push_back( &SnapshotDatas, (content_t*)snapshotdata );
	}
	else{
		__snapshotdata_destroy( snapshotdata );
	}
	return res;
}

//////////////////////////////////////////////////////////////////////////
int __snapshotdata_create_in_memory( dev_t orig_dev_id, stream_size_t snapshotdatasize, snapshotdata_t** pp_snapshotdata)
{
	int res = SUCCESS;

	blk_dev_info_t info_dev = { 0 };
	snapshotdata_t* p_snapshotdata = (snapshotdata_t*)content_new( &SnapshotDatas );
	if (p_snapshotdata == NULL){
		log_errorln( "Cannot allocate snapshot data struct." );
		return -ENOMEM;
	}

	log_traceln_p( "Creating in memory. snapshotdata=", p_snapshotdata );

	p_snapshotdata->user_process_id = task_pid_nr( current );
	log_traceln_d( "current task id=", p_snapshotdata->user_process_id );

	log_traceln_sz( "try to allocate =", (size_t)snapshotdatasize );
	p_snapshotdata->err_code = SUCCESS;
	p_snapshotdata->type = SNAPSHOTDATA_TYPE_MEM;
	do{
		p_snapshotdata->mem = dbg_kzalloc( sizeof( snapshotdata_mem_t ), GFP_KERNEL );
		if (p_snapshotdata->mem == NULL){
			res = -ENOMEM;
			break;
		}

		p_snapshotdata->mem->buff_size = (size_t)snapshotdatasize;
		p_snapshotdata->dev_id = orig_dev_id;

		res = blk_dev_get_info( p_snapshotdata->dev_id, &info_dev );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot obtain info about original device=.", p_snapshotdata->dev_id );
			break;
		}

		p_snapshotdata->mem->buff = dbg_vmalloc( p_snapshotdata->mem->buff_size );
		if (p_snapshotdata->mem->buff == NULL){
			res = -ENOMEM;
			break;
		}

		log_traceln_sz( "Memory size allocated =", p_snapshotdata->mem->buff_size );
		__snapshotdata_create_blk_info( &p_snapshotdata->mem->mem_blk_info, sector_from_size( p_snapshotdata->mem->buff_size ) );

		res = __snapshotdata_create_common( p_snapshotdata, info_dev.count_sect );
		if (res != SUCCESS){
			break;
		}

		snapshotdata_Reset( p_snapshotdata );
	} while (false);

	if (res == SUCCESS){
		*pp_snapshotdata = p_snapshotdata;
		container_push_back( &SnapshotDatas, (content_t*)p_snapshotdata );
	}
	else{
		__snapshotdata_destroy( p_snapshotdata );
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_destroy( snapshotdata_t* p_snapshotdata )
{
	if (p_snapshotdata == NULL)
		return -ENODATA;

	{
		unsigned long wrote_mb = (unsigned long)(atomic64_read( &p_snapshotdata->state_sectors_write ) >> (20 - SECTOR512_SHIFT));

		log_traceln_ld( "Snapshot data filled MiB ", wrote_mb );
	}

	if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_INVALID){
		log_errorln( "Snapshotdata already destroyed." );
		return -EALREADY;
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_DISK){

		if (p_snapshotdata->disk->dsk_datarangeset){
			rangeset_destroy( p_snapshotdata->disk->dsk_datarangeset );
			p_snapshotdata->disk->dsk_datarangeset = NULL;
		}

		if (p_snapshotdata->disk->dsk_blk_dev != NULL){
			blk_dev_close( p_snapshotdata->disk->dsk_blk_dev );
			p_snapshotdata->disk->dsk_blk_dev = NULL;
			log_traceln_dev_t( "device=", p_snapshotdata->disk->dsk_blk_dev_id );
		}
		dbg_kfree( p_snapshotdata->disk );
		p_snapshotdata->disk = NULL;

	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_MEM){
		if (p_snapshotdata->mem->buff != NULL){
			dbg_vfree( p_snapshotdata->mem->buff, p_snapshotdata->mem->buff_size );
			p_snapshotdata->mem->buff = NULL;
		}
		dbg_kfree( p_snapshotdata->mem );
		p_snapshotdata->mem = NULL;

	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_COMMON){
		snapshotdata_common_disk_t* common = p_snapshotdata->common;
		p_snapshotdata->common = NULL;

		snapshotdata_common_disk_put_resource( common );
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		snapshotdata_stretch_disk_t* stretch = p_snapshotdata->stretch;
		p_snapshotdata->stretch = NULL;

		snapshotdata_stretch_disk_put_resource( stretch );
	}
	p_snapshotdata->type = SNAPSHOTDATA_TYPE_INVALID;

	sparse_array_1lv_done( &p_snapshotdata->accordance_map );
	sparse_array_1lv_done( &p_snapshotdata->write_acc_map );

#ifdef SNAPDATA_ZEROED
	if (get_zerosnapdata( )){
	rangevector_destroy( &p_snapshotdata->zero_sectors );
	log_traceln( "Zero sectors bitmap destroy." );
	}
#endif

	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_mem_direct_write( snapshotdata_mem_t* mem, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	page_array_copy2mem(
		(char*)(mem->buff) + sector_to_size( blk_ofs ),
		sector_to_size( arr_ofs ),
		arr,
		sector_to_size( blk_cnt )
	);
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_disk_direct_write( snapshotdata_disk_t* disk, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	int res = SUCCESS;
	sector_t ofs = 0;
	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangeset_v2p( disk->dsk_datarangeset, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length.", 0 - res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = blk_dev_direct_submit_pages(
			disk->dsk_blk_dev,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);
		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot.", 0 - res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_direct_write( snapshotdata_common_disk_t* common, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	sector_t ofs = 0;
	int res = SUCCESS;

	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangeset_v2p( common->cmn_datarangeset, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length.", 0 - res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = blk_dev_direct_submit_pages(
			common->cmn_blk_dev,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);

		if (processed != real_size){
			res = -EIO;
			log_errorln( "Cannot direct write data to snapshot." );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_stretch_direct_write( snapshotdata_stretch_disk_t* stretch, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	sector_t ofs = 0;
	int res = SUCCESS;

	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangevector_v2p( &stretch->stretch_dataranges, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length. error code=", 0 - res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = blk_dev_direct_submit_pages(
			stretch->stretch_blk_dev,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);

		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot.", 0 - res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_write_direct( snapshotdata_t* p_snapshotdata, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	atomic64_add( blk_cnt, &p_snapshotdata->state_sectors_write );
	switch (p_snapshotdata->type){
	case SNAPSHOTDATA_TYPE_DISK:
		return snapshotdata_disk_direct_write( p_snapshotdata->disk, arr_ofs, arr, blk_ofs, blk_cnt );
	case SNAPSHOTDATA_TYPE_MEM:
		return snapshotdata_mem_direct_write( p_snapshotdata->mem, arr_ofs, arr, blk_ofs, blk_cnt );
	case SNAPSHOTDATA_TYPE_COMMON:
		return snapshotdata_common_direct_write( p_snapshotdata->common, arr_ofs, arr, blk_ofs, blk_cnt );
	case SNAPSHOTDATA_TYPE_STRETCH:
		return snapshotdata_stretch_direct_write( p_snapshotdata->stretch, arr_ofs, arr, blk_ofs, blk_cnt );
	}

	return -EINVAL;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_pos_increnent( snapshotdata_blk_info_t* blk_info, sector_t blk_cnt, sector_t * p_blk_pos )
{
	int res = SUCCESS;

	spin_lock( &blk_info->locker );
	if ((blk_info->pos + blk_cnt) < blk_info->cnt){
		*p_blk_pos = blk_info->pos;
		blk_info->pos += blk_cnt;
	}else
		res = -EINVAL;
	spin_unlock( &blk_info->locker );

	return res;
}
//////////////////////////////////////////////////////////////////////////

int snapshotdata_mem_direct_write_dio( snapshotdata_mem_t* mem, dio_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	page_array_copy2mem(
		(char*)(mem->buff) + sector_to_size( blk_ofs ),
		sector_to_size( arr_ofs ),
		arr,
		sector_to_size( blk_cnt )
		);
	__dio_bio_end_io( dio_req, blk_cnt, SUCCESS );

	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_disk_direct_write_dio( snapshotdata_disk_t* disk, dio_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	int res = SUCCESS;
	sector_t ofs = 0;
	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangeset_v2p( disk->dsk_datarangeset, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length.", 0 - res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = dio_submit_pages(
			disk->dsk_blk_dev,
			dio_req,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);

		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot.", 0 - res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_common_direct_write_dio( snapshotdata_common_disk_t* common, dio_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	sector_t ofs = 0;
	int res = SUCCESS;

	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangeset_v2p( common->cmn_datarangeset, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length.", 0 - res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = dio_submit_pages(
			common->cmn_blk_dev,
			dio_req,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
		);

		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot.", 0 - res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_stretch_direct_write_dio( snapshotdata_stretch_disk_t* stretch, dio_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	sector_t ofs = 0;
	int res = SUCCESS;

	while (ofs < blk_cnt){
		sector_t real_ofs;
		sector_t real_size;
		sector_t processed;
		res = rangevector_v2p( &stretch->stretch_dataranges, blk_ofs + ofs, blk_cnt - ofs, &real_ofs, &real_size );
		if (res != SUCCESS){
			log_errorln_d( "Cannot get real offset and length.", 0 - res );
			log_errorln_sect( "blk_ofs + ofs=", blk_ofs + ofs );
			log_errorln_sect( "blk_cnt - ofs=", blk_cnt - ofs );
			break;
		}

		processed = dio_submit_pages(
			stretch->stretch_blk_dev,
			dio_req,
			WRITE,
			arr_ofs + ofs,
			arr,
			real_ofs,
			real_size
			);

		if (processed != real_size){
			log_errorln_d( "Cannot direct write data to snapshot.", 0 - res );
			log_errorln_sect( "real_ofs=", real_ofs );
			log_errorln_sect( "real_size=", real_size );
			log_errorln_sect( "processed=", processed );
			break;
		}

		ofs += real_size;
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_write_dio_direct( snapshotdata_t* p_snapshotdata, dio_request_t* dio_req, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	atomic64_add( blk_cnt, &p_snapshotdata->state_sectors_write );
	switch (p_snapshotdata->type){
	case SNAPSHOTDATA_TYPE_DISK:
		return snapshotdata_disk_direct_write_dio( p_snapshotdata->disk, dio_req, arr_ofs, arr, blk_ofs, blk_cnt );
	case SNAPSHOTDATA_TYPE_MEM:
		return snapshotdata_mem_direct_write_dio( p_snapshotdata->mem, dio_req, arr_ofs, arr, blk_ofs, blk_cnt );
	case SNAPSHOTDATA_TYPE_COMMON:
		return snapshotdata_common_direct_write_dio( p_snapshotdata->common, dio_req, arr_ofs, arr, blk_ofs, blk_cnt );
	case SNAPSHOTDATA_TYPE_STRETCH:
		return snapshotdata_stretch_direct_write_dio( p_snapshotdata->stretch, dio_req, arr_ofs, arr, blk_ofs, blk_cnt );
	}
	return -EINVAL;
}

int __snapshotdata_write_dio_request_to( snapshotdata_t* p_snapshotdata, sparse_arr1lv_t* to_map, dio_request_t* dio_req )
{
	int res = -EFAULT;
	int dio_inx;
	sector_t block_inx;
	sector_t blk_pos;
	snapshotdata_blk_info_t* blk_info = NULL;
	switch (p_snapshotdata->type){
	case SNAPSHOTDATA_TYPE_MEM: blk_info = &p_snapshotdata->mem->mem_blk_info; break;
	case SNAPSHOTDATA_TYPE_DISK: blk_info = &p_snapshotdata->disk->dsk_blk_info; break;
	case SNAPSHOTDATA_TYPE_COMMON: blk_info = &p_snapshotdata->common->cmn_blk_info; break;
	case SNAPSHOTDATA_TYPE_STRETCH: blk_info = &p_snapshotdata->stretch->stretch_blk_info; break;
	}

	if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		if ((blk_info->cnt - blk_info->pos) < p_snapshotdata->stretch->stretch_empty_limit){
			snapshotdata_stretch_halffill( p_snapshotdata->stretch, sector_to_streamsize(blk_info->pos) );
		}
	}
	for (dio_inx = 0; dio_inx < dio_req->dios_cnt; ++dio_inx){
		dio_t* dio = dio_req->dios[dio_inx];

		res = __snapshotdata_pos_increnent( blk_info, dio->sect_len, &blk_pos );
		if (res != SUCCESS){
			if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
				snapshotdata_stretch_overflow( p_snapshotdata->stretch, res );
			}

			log_errorln_sect( "blk_pos + size = ", (blk_info->pos + dio->sect_len) );
			log_errorln_sect( "blk_cnt = ", blk_info->cnt );
			log_errorln( "Cannot store data to snapshot. Not enough space." );
			return -ENODATA;
		}

		res = __snapshotdata_write_dio_direct( p_snapshotdata, dio_req, 0, dio->buff, blk_pos, dio->sect_len );
		if (res != SUCCESS){
			log_errorln( "Failed to call _snapshotdata_write_direct." );
			return res;
		}

		for (block_inx = 0; block_inx < dio->sect_len; block_inx += SNAPSHOTDATA_BLK_SIZE){
			res = sparse_array_1lv_set(
				to_map,
				(dio->sect_ofs + block_inx) >> SNAPSHOTDATA_BLK_SHIFT,
				(sparse_array_el_t)((blk_pos + block_inx) >> SNAPSHOTDATA_BLK_SHIFT),
				NULL
			);
			if (res != SUCCESS){
				log_errorln( "Cannot set accordance map." );
				break;
			}
		}
		if (res != SUCCESS)
			break;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_write_to( snapshotdata_t* p_snapshotdata, sparse_arr1lv_t* to_map, sector_t arr_ofs, page_array_t* arr, sector_t blk_ofs, sector_t blk_cnt )
{
	int res;
	sector_t block_inx;
	sector_t blk_pos;
	snapshotdata_blk_info_t* blk_info = NULL;
	switch (p_snapshotdata->type){
	case SNAPSHOTDATA_TYPE_MEM: blk_info = &p_snapshotdata->mem->mem_blk_info; break;
	case SNAPSHOTDATA_TYPE_DISK: blk_info = &p_snapshotdata->disk->dsk_blk_info; break;
	case SNAPSHOTDATA_TYPE_COMMON: blk_info = &p_snapshotdata->common->cmn_blk_info; break;
	case SNAPSHOTDATA_TYPE_STRETCH: blk_info = &p_snapshotdata->stretch->stretch_blk_info; break;
	}

	if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		if ((blk_info->cnt - blk_info->pos) < p_snapshotdata->stretch->stretch_empty_limit){
			snapshotdata_stretch_halffill( p_snapshotdata->stretch, sector_to_streamsize( blk_info->pos ) );
		}
	}

	res = __snapshotdata_pos_increnent( blk_info, blk_cnt, &blk_pos );
	if (res != SUCCESS){
		if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
			snapshotdata_stretch_overflow( p_snapshotdata->stretch, res );
		}

		log_errorln_sect( "blk_pos + size = ", (blk_info->pos + blk_cnt) );
		log_errorln_sect( "blk_cnt = ", blk_info->cnt );
		log_errorln( "Cannot store data to snapshot. Not enough space." );
		return -ENODATA;
	}

	res = __snapshotdata_write_direct( p_snapshotdata, arr_ofs, arr, blk_pos, blk_cnt );
	if (res != SUCCESS){
		log_errorln( "Failed to call _snapshotdata_write_direct." );
		return res;
	}

	for (block_inx = 0; block_inx < blk_cnt; block_inx += SNAPSHOTDATA_BLK_SIZE){
		res = sparse_array_1lv_set(
			to_map,
			(blk_ofs + block_inx) >> SNAPSHOTDATA_BLK_SHIFT,
			(sparse_array_el_t)((blk_pos + block_inx) >> SNAPSHOTDATA_BLK_SHIFT),
			NULL
		);
		if (res != SUCCESS){
			log_errorln( "Cannot set accordance map." );
			break;
		}
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
void snapshotdata_SetCorrupted( snapshotdata_t* p_snapshotdata, int err_code )
{
	if (!p_snapshotdata->corrupted){
		atomic_set( &p_snapshotdata->corrupted_cnt, 0 );
		p_snapshotdata->corrupted = true;
		p_snapshotdata->err_code = err_code;

		log_errorln_d( "Now snapshot corrupted. error=", (0-err_code) );
	}
	else{
		log_errorln( "Snapshot already corrupted." );
	}
}

bool snapshotdata_IsCorrupted( snapshotdata_t* p_snapshotdata, dev_t original_dev_id )
{
	if (p_snapshotdata == NULL)
		return true;

	if (p_snapshotdata->corrupted){
		if (0 == atomic_read( &p_snapshotdata->corrupted_cnt )){
			log_errorln_dev_t( "Snapshot is corrupted for device ", original_dev_id );
		}
		atomic_inc( &p_snapshotdata->corrupted_cnt );
		return true;
	}
	else
		return false;
}

int __snapshotdata_test_block( sparse_arr1lv_t* to_acc_map, sector_t blk_ofs, bool* p_in_snapshot )
{
	int res = SUCCESS;
	sparse_array_el_t el;

	res = sparse_array_1lv_get( to_acc_map, (blk_ofs >> SNAPSHOTDATA_BLK_SHIFT), &el );
	if (res == SUCCESS){
		*p_in_snapshot = true;
	}
	else if (res == -ENODATA){
		*p_in_snapshot = false;
		res = SUCCESS;
	}
	else{
		log_errorln_sect( "Cannot get data from snapshot map by offset=", blk_ofs );
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_get_snapofs( snapshotdata_t* snapshotdata, sector_t blk_ofs, sector_t* snap_ofs )
{
	sparse_array_el_t el;
	int res = SUCCESS;

	res = sparse_array_1lv_get( &snapshotdata->write_acc_map, (blk_ofs >> SNAPSHOTDATA_BLK_SHIFT), &el );
	if (res != SUCCESS){
		res = sparse_array_1lv_get( &snapshotdata->accordance_map, (blk_ofs >> SNAPSHOTDATA_BLK_SHIFT), &el );
		if (res != SUCCESS){
			return res;
		}
	}
	*snap_ofs = ((sector_t)(el) << SNAPSHOTDATA_BLK_SHIFT);

	return res;
}

int snapshotdata_TestBlockInBothMap( snapshotdata_t* p_snapshotdata, sector_t blk_ofs, bool* p_in_snapshot )
{
	int res;
	sector_t snap_ofs;

	res = snapshotdata_get_snapofs( p_snapshotdata, blk_ofs, &snap_ofs );
	if (res == SUCCESS){
		*p_in_snapshot = true;
	}
	else{
		*p_in_snapshot = false;
		if (res == -ENODATA)
			res = SUCCESS;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int _snapshotdata_read_page_direct( snapshotdata_t* p_snapshotdata, page_info_t pg, sector_t snap_ofs )
{
	int res = SUCCESS;

	if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_DISK){
		sector_t real_ofs;
		sector_t real_size;

		res = rangeset_v2p( p_snapshotdata->disk->dsk_datarangeset, snap_ofs, (PAGE_SIZE / SECTOR512), &real_ofs, &real_size );
		if (res == SUCCESS){
			res = blk_dev_direct_submit_page( p_snapshotdata->disk->dsk_blk_dev, READ, real_ofs, pg.page );
			if (res != SUCCESS){
				log_errorln_sect( "Failed to read data from snapshot. real_ofs=", real_ofs );
			}
		}else{
			log_errorln( "Cannot do range conversion." );
		}
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_COMMON){
		sector_t real_ofs;
		sector_t real_size;
		res = rangeset_v2p( p_snapshotdata->common->cmn_datarangeset, snap_ofs, (PAGE_SIZE / SECTOR512), &real_ofs, &real_size );
		if (res == SUCCESS){
			res = blk_dev_direct_submit_page( p_snapshotdata->common->cmn_blk_dev, READ, real_ofs, pg.page );
			if (res != SUCCESS){
				log_errorln_sect( "Failed to read data from snapshot. real_ofs=", real_ofs );
			}
		}
		else{
			log_errorln( "Cannot do range conversion." );
		}
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		sector_t real_ofs;
		sector_t real_size;
		res = rangevector_v2p( &p_snapshotdata->stretch->stretch_dataranges, snap_ofs, (PAGE_SIZE / SECTOR512), &real_ofs, &real_size );
		if (res == SUCCESS){
			res = blk_dev_direct_submit_page( p_snapshotdata->stretch->stretch_blk_dev, READ, real_ofs, pg.page );
			if (res != SUCCESS){
				log_errorln_sect( "Failed to read data from snapshot. real_ofs=", real_ofs );
			}
		}
		else{
			log_errorln( "Cannot do range conversion." );
		}
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_MEM){
		memcpy( pg.addr, (char*)(p_snapshotdata->mem->buff) + sector_to_size( snap_ofs ), PAGE_SIZE );
	}
	else{
		res = -EINVAL;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int _snapshotdata_read_part_direct( snapshotdata_t* p_snapshotdata, redirect_bio_endio_t* rq_endio, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt )
{
	int res = SUCCESS;

	if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_DISK){
		sector_t ofs = 0;
		while (ofs < blk_cnt){
			sector_t real_ofs;
			sector_t real_size;

			res = rangeset_v2p( p_snapshotdata->disk->dsk_datarangeset, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
			if (res != SUCCESS){
				log_errorln( "Cannot do range conversion." );
				break;
			}

			res = blk_dev_redirect_part( rq_endio, p_snapshotdata->disk->dsk_blk_dev, real_ofs, rq_ofs + ofs, real_size );

			if (res != SUCCESS){
				log_errorln( "Failed to read data from snapshot." );
				break;
			}
			ofs += real_size;
		}
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_COMMON){
		sector_t ofs = 0;
		while (ofs < blk_cnt){
			sector_t real_ofs;
			sector_t real_size;
			res = rangeset_v2p( p_snapshotdata->common->cmn_datarangeset, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
			if (res != SUCCESS){
				log_errorln( "Cannot do range conversion." );
				break;
			}
			res = blk_dev_redirect_part( rq_endio, p_snapshotdata->common->cmn_blk_dev, real_ofs, rq_ofs + ofs, real_size );
			if (res != SUCCESS){
				log_errorln( "Failed to read data from snapshot." );
				break;
			}
			ofs += real_size;
		}
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		sector_t ofs = 0;
		while (ofs < blk_cnt){
			sector_t real_ofs;
			sector_t real_size;
			res = rangevector_v2p( &p_snapshotdata->stretch->stretch_dataranges, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
			if (res != SUCCESS){
				log_errorln( "Cannot do range conversion." );
				break;
			}
			res = blk_dev_redirect_part( rq_endio, p_snapshotdata->stretch->stretch_blk_dev, real_ofs, rq_ofs + ofs, real_size );
			if (res != SUCCESS){
				log_errorln( "Failed to read data from snapshot." );
				break;
			}
			ofs += real_size;
		}
	}
	else if (p_snapshotdata->type == SNAPSHOTDATA_TYPE_MEM){
		char* snapdatabuff = (char*)(p_snapshotdata->mem->buff);
		blk_dev_memcpy_request_part(
			rq_endio,
			snapdatabuff + sector_to_size( snap_ofs ),
			rq_ofs,
			blk_cnt
		);
	}
	else{
		res = -EINVAL;
	}
	return res;
}

//////////////////////////////////////////////////////////////////////////
int snapshotdata_read_part( snapshotdata_t* p_snapshotdata, redirect_bio_endio_t* rq_endio, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count )
{
	int res = SUCCESS;
	sector_t buffer_ofs = 0;
	sector_t snap_ofs_start = 0;
	sector_t snap_ofs_prev = 0;
	sector_t snap_ofs_curr = 0;
	sector_t range_blk_length = 0;
	sector_t blk_ofs_inx = 0;

	sector_t unordered_ofs;
	sector_t prev_min_len = 0;

	unordered_ofs = (blk_ofs + blk_ofs_inx) & SNAPSHOTDATA_BLK_MASK;
	{
		res = snapshotdata_get_snapofs( p_snapshotdata, (blk_ofs + blk_ofs_inx), &snap_ofs_curr );
		if (res != SUCCESS){
			return res;
		}
		snap_ofs_curr |= unordered_ofs;

		prev_min_len = min_t( sector_t, (SNAPSHOTDATA_BLK_SIZE - unordered_ofs), (rq_count - blk_ofs_inx) );

		snap_ofs_prev = snap_ofs_curr;
		snap_ofs_start = snap_ofs_curr;
		range_blk_length = prev_min_len;

		blk_ofs_inx += prev_min_len;
	}

	for (; blk_ofs_inx < rq_count; blk_ofs_inx += SNAPSHOTDATA_BLK_SIZE){
		sector_t min_len;

		res = snapshotdata_get_snapofs( p_snapshotdata, (blk_ofs + blk_ofs_inx), &snap_ofs_curr );
		if (res != SUCCESS){
			return res;
		}

		min_len = min_t( sector_t, SNAPSHOTDATA_BLK_SIZE, (rq_count - blk_ofs_inx) );

		if ((snap_ofs_prev + prev_min_len) == snap_ofs_curr){
			snap_ofs_prev = snap_ofs_curr;
			range_blk_length += min_len;
			prev_min_len = min_len;
			continue;
		}

		res = _snapshotdata_read_part_direct( p_snapshotdata, rq_endio, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length );
		if (res != SUCCESS){
			break;
		}

		buffer_ofs += range_blk_length;

		snap_ofs_start = snap_ofs_curr;
		snap_ofs_prev = snap_ofs_curr;

		range_blk_length = min_len;

		prev_min_len = min_len;
	}
	if (res == SUCCESS){
		if (range_blk_length){
			res = _snapshotdata_read_part_direct( p_snapshotdata, rq_endio, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length );
		}
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __snapshotdata_read_dio_direct( snapshotdata_t* snapshotdata, dio_request_t* dio_req, sector_t snap_ofs, sector_t rq_ofs, sector_t blk_cnt, page_array_t* arr )
{
	int res = SUCCESS;

	if (snapshotdata->type == SNAPSHOTDATA_TYPE_DISK){
		sector_t ofs = 0;
		while (ofs < blk_cnt){
			sector_t real_ofs;
			sector_t real_size;

			res = rangeset_v2p( snapshotdata->disk->dsk_datarangeset, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
			if (res != SUCCESS){
				log_errorln( "Cannot do range conversion." );
				break;
			}

			if (real_size != dio_submit_pages( snapshotdata->disk->dsk_blk_dev, dio_req, READ, rq_ofs + ofs, arr, real_ofs, real_size )){
				log_errorln( "Failed to read data from snapshot." );
				res = -EFAULT;
				break;
			}

			ofs += real_size;
		}
	}
	else if (snapshotdata->type == SNAPSHOTDATA_TYPE_COMMON){
		sector_t ofs = 0;
		while (ofs < blk_cnt){
			sector_t real_ofs;
			sector_t real_size;
			res = rangeset_v2p( snapshotdata->common->cmn_datarangeset, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
			if (res != SUCCESS){
				log_errorln( "Cannot do range conversion." );
				break;
			}

			if (real_size != dio_submit_pages( snapshotdata->common->cmn_blk_dev, dio_req, READ, rq_ofs + ofs, arr, real_ofs, real_size )){
				log_errorln( "Failed to read data from snapshot." );
				res = -EFAULT;
				break;
			}

			ofs += real_size;
		}
	}
	else if (snapshotdata->type == SNAPSHOTDATA_TYPE_STRETCH){
		sector_t ofs = 0;
		while (ofs < blk_cnt){
			sector_t real_ofs;
			sector_t real_size;
			res = rangevector_v2p( &snapshotdata->stretch->stretch_dataranges, snap_ofs + ofs, (blk_cnt - ofs), &real_ofs, &real_size );
			if (res != SUCCESS){
				log_errorln( "Cannot do range conversion." );
				break;
			}

			if (real_size != dio_submit_pages( snapshotdata->stretch->stretch_blk_dev, dio_req, READ, rq_ofs + ofs, arr, real_ofs, real_size )){
				log_errorln( "Failed to read data from snapshot." );
				res = -EFAULT;
				break;
			}
			ofs += real_size;
		}
	}
	else if (snapshotdata->type == SNAPSHOTDATA_TYPE_MEM){
		char* snapdatabuff = (char*)(snapshotdata->mem->buff);
		dio_memcpy_read( snapdatabuff + sector_to_size( snap_ofs ), dio_req, arr, rq_ofs, blk_cnt );
	}
	else{
		res = -EINVAL;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_read_dio( snapshotdata_t* snapshotdata, dio_request_t* dio_req, sector_t blk_ofs, sector_t rq_ofs, sector_t rq_count, page_array_t* arr )
{
	int res = SUCCESS;
	sector_t buffer_ofs = 0;
	sector_t snap_ofs_start = 0;
	sector_t snap_ofs_prev = 0;
	sector_t snap_ofs_curr = 0;
	sector_t range_blk_length = 0;
	sector_t blk_ofs_inx = 0;

	sector_t unordered_ofs;
	sector_t prev_min_len = 0;

	unordered_ofs = (blk_ofs + blk_ofs_inx) & SNAPSHOTDATA_BLK_MASK;
	{
		sparse_array_el_t el;
		res = sparse_array_1lv_get( &snapshotdata->accordance_map, (blk_ofs + blk_ofs_inx) >> SNAPSHOTDATA_BLK_SHIFT, &el );
		if (res != SUCCESS){
			return res;
		}
		snap_ofs_curr = ((sector_t)(el) << SNAPSHOTDATA_BLK_SHIFT) | unordered_ofs;

		prev_min_len = SNAPSHOTDATA_BLK_SIZE;

		snap_ofs_prev = snap_ofs_curr;
		snap_ofs_start = snap_ofs_curr;
		range_blk_length = prev_min_len;

		blk_ofs_inx += prev_min_len;
	}

	for (; blk_ofs_inx < rq_count; blk_ofs_inx += SNAPSHOTDATA_BLK_SIZE){
		sparse_array_el_t el;
		res = sparse_array_1lv_get( &snapshotdata->accordance_map, (blk_ofs + blk_ofs_inx) >> SNAPSHOTDATA_BLK_SHIFT, &el );
		if (res != SUCCESS){
			return res;
		}

		snap_ofs_curr = ((sector_t)(el) << SNAPSHOTDATA_BLK_SHIFT);

		if ((snap_ofs_prev + prev_min_len) == snap_ofs_curr){
			snap_ofs_prev = snap_ofs_curr;
			range_blk_length += SNAPSHOTDATA_BLK_SIZE;
			prev_min_len = SNAPSHOTDATA_BLK_SIZE;
			continue;
		}

		res = __snapshotdata_read_dio_direct( snapshotdata, dio_req, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length, arr );
		if (res != SUCCESS){
			break;
		}

		buffer_ofs += range_blk_length;

		snap_ofs_start = snap_ofs_curr;
		snap_ofs_prev = snap_ofs_curr;

		range_blk_length = SNAPSHOTDATA_BLK_SIZE;

		prev_min_len = SNAPSHOTDATA_BLK_SIZE;
	}
	if (res == SUCCESS){
		if (range_blk_length){
			res = __snapshotdata_read_dio_direct( snapshotdata, dio_req, snap_ofs_start, rq_ofs + buffer_ofs, range_blk_length, arr );
		}
	}
	return res;
}

//////////////////////////////////////////////////////////////////////////
int snapshotdata_write_to_image( snapshotdata_t* snapshotdata, struct bio* bio, struct block_device* defer_io_blkdev )
{
	int res = SUCCESS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	struct bio_vec* bvec;
	unsigned short iter;
#else
	struct bio_vec bvec;
	struct bvec_iter iter;
#endif
	sector_t ord_ofs_sect;
	sector_t ord_len_sect;
	sector_t ofs_sect = bio_bi_sector( bio );
	sector_t len_sect = sector_from_size( bio_bi_size( bio ) );
	sector_t arr_ofs = 0;
	page_array_t* arr = NULL;

	snapshotdata_order_border( ofs_sect, len_sect, &ord_ofs_sect, &ord_len_sect );

	arr = page_array_alloc( page_count_calculate( ord_ofs_sect, ord_len_sect ), GFP_NOIO );
	if (arr == NULL){
		log_errorln_sect( "Cannot allocate some pages for sectors. Sectors count=", ord_len_sect );
		return -ENOMEM;
	}
	do{
		if ( ofs_sect != ord_ofs_sect ){ //preread first page needed
			sector_t first_blk = ord_ofs_sect;
			sector_t snap_ofs;

			if (SUCCESS == snapshotdata_get_snapofs( snapshotdata, first_blk, &snap_ofs )){
				res = _snapshotdata_read_page_direct( snapshotdata, arr->pg[0], snap_ofs );
			}
			else{
				res = blk_dev_direct_submit_page( defer_io_blkdev, READ, first_blk, arr->pg[0].page );
			}
			if (res != SUCCESS){
				log_errorln_sect( "Cannot preread first block. first_blk=", first_blk );
				break;
			}

		}
		if ((ofs_sect != ord_ofs_sect) && (ord_len_sect == SNAPSHOTDATA_BLK_SIZE)){
			//first and last blocks are same block and already preread
		}else{
			if ((ord_len_sect - len_sect) > (ofs_sect - ord_ofs_sect)){ //preread last page needed
				sector_t last_blk = ord_ofs_sect + ord_len_sect - SNAPSHOTDATA_BLK_SIZE;
				sector_t snap_ofs;

				if (SUCCESS == snapshotdata_get_snapofs( snapshotdata, last_blk, &snap_ofs )){
					res = _snapshotdata_read_page_direct( snapshotdata, arr->pg[arr->count - 1], snap_ofs );
				}
				else{
					res = blk_dev_direct_submit_page( defer_io_blkdev, READ, last_blk, arr->pg[arr->count - 1].page );
				}
				if (res != SUCCESS){
					log_errorln_sect( "Cannot preread last block. last_blk=", last_blk );
					break;
				}
			}
		}
		arr_ofs = ofs_sect - ord_ofs_sect;
		bio_for_each_segment( bvec, bio, iter )
		{
			char* mem;
			unsigned int bvec_ofs;

			for (bvec_ofs = 0; bvec_ofs < bio_vec_len( bvec ); bvec_ofs += SECTOR512){
				void* dst = page_get_sector( arr, arr_ofs );

				mem = mem_kmap_atomic( bio_vec_page( bvec ) );
				memcpy( dst, mem + bio_vec_offset( bvec ) + bvec_ofs, SECTOR512 );
				mem_kunmap_atomic( mem );

				++arr_ofs;
			}
		}

		if (res == SUCCESS){
			res = __snapshotdata_write_to( snapshotdata, &snapshotdata->write_acc_map, 0, arr, ord_ofs_sect, ord_len_sect );
		}

	} while (false);
	page_array_free( arr );
	arr = NULL;

	if (res != SUCCESS){
		log_errorln_sect( "Failed. ofs=", ofs_sect );
		log_errorln_sect( "        len=", len_sect );

		log_errorln_sect( "ord_ofs_sect=", ord_ofs_sect );
		log_errorln_sect( "ord_len_sect=", ord_len_sect );
	}
	return res;
}

//////////////////////////////////////////////////////////////////////////
int snapshotdata_stretch_Init( void )
{
	return container_init( &SnapshotStretchDisks, sizeof( snapshotdata_stretch_disk_t ), NULL);
}
//////////////////////////////////////////////////////////////////////////
int snapshotdata_stretch_Done( void )
{
	content_t* content;
	snapshotdata_stretch_disk_t* disk;

	CONTAINER_FOREACH_BEGIN( SnapshotStretchDisks, content )
	{
		disk = (snapshotdata_stretch_disk_t*)content;
		log_errorln_uuid( "Still in use. id=", disk->unique_id );
		log_errorln_dev_t( "device=", disk->stretch_blk_dev_id );
		log_errorln_d( "owners count=", atomic_read( &disk->sharing_header.own_cnt ) );

	}CONTAINER_FOREACH_END( SnapshotStretchDisks );

	return container_done( &SnapshotStretchDisks );
}
//////////////////////////////////////////////////////////////////////////
void snapshotdata_stretch_free( snapshotdata_stretch_disk_t* stretch_disk )
{
	log_traceln_uuid( "id=", stretch_disk->unique_id );

	snapshotdata_stretch_terminate( stretch_disk );
	{
		ctrl_pipe_t* pipe = stretch_disk->ctrl_pipe;
		stretch_disk->ctrl_pipe = NULL;

		ctrl_pipe_put_resource( pipe );
	}

	rangevector_destroy( &stretch_disk->stretch_dataranges );

	if (stretch_disk->stretch_blk_dev != NULL){
		blk_dev_close( stretch_disk->stretch_blk_dev );
		stretch_disk->stretch_blk_dev = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////

void snapshotdata_stretch_free_cb( void* this_resource )
{
	snapshotdata_stretch_disk_t* stretch_disk = (snapshotdata_stretch_disk_t*)this_resource;

	snapshotdata_stretch_free( stretch_disk );

	container_free( &stretch_disk->content );
}

//////////////////////////////////////////////////////////////////////////

snapshotdata_stretch_disk_t* snapshotdata_stretch_create( unsigned char* id, dev_t dev_id )
{
	int res = SUCCESS;
	snapshotdata_stretch_disk_t* stretch_disk = NULL;

	log_traceln_uuid( "id=", id );
	log_traceln_dev_t( "dev_id=", dev_id );

	stretch_disk = (snapshotdata_stretch_disk_t*)content_new( &SnapshotStretchDisks );
	if (stretch_disk == NULL)
		return NULL;

	do{
		res = blk_dev_open( dev_id, &stretch_disk->stretch_blk_dev );
		if (res != SUCCESS){
			log_errorln( "Failed to open snapshot device." );
			break;
		}

		shared_resource_init( &stretch_disk->sharing_header, stretch_disk, snapshotdata_stretch_free_cb );
		__snapshotdata_create_blk_info( &stretch_disk->stretch_blk_info, 0 );

		rangevector_create( &stretch_disk->stretch_dataranges, true );

		stretch_disk->stretch_blk_dev_id = dev_id;
		memcpy(stretch_disk->unique_id, id, 16);

		stretch_disk->stretch_empty_limit = (sector_t)(64 * 1024 * 1024 / SECTOR512); //by default value
		stretch_disk->halffilled = false;
		stretch_disk->overflowed = false;

		container_push_back( &SnapshotStretchDisks, &stretch_disk->content );

		log_traceln( "New snapshot stretch added to container." );

	} while (false);

	if (res != SUCCESS){
		if (stretch_disk->stretch_blk_dev != NULL){
			blk_dev_close( stretch_disk->stretch_blk_dev );
			stretch_disk->stretch_blk_dev = NULL;
		}
		content_free( &stretch_disk->content );
		stretch_disk = NULL;
	}

	return stretch_disk;
}

snapshotdata_stretch_disk_t* snapshotdata_stretch_get( unsigned char id[16] )
{
	snapshotdata_stretch_disk_t* result = NULL;
	content_t* content = NULL;

	CONTAINER_FOREACH_BEGIN( SnapshotStretchDisks, content )
	{
		snapshotdata_stretch_disk_t* stretch_disk = (snapshotdata_stretch_disk_t*) content;

		if (0 == memcmp( stretch_disk->unique_id, id, 16 )){
			result = stretch_disk;
			break;
		}
	}
	CONTAINER_FOREACH_END( SnapshotStretchDisks )

	return result;
}

int snapshotdata_stretch_add_dev( snapshotdata_stretch_disk_t* stretch_disk, dev_t dev_id )
{
	int res = SUCCESS;
	bool is_compatible = false;
	blk_dev_info_t info_dev;
	snapshotdata_t* snapdata = NULL;

	log_traceln_dev_t( "device=", dev_id );

	res = __snapshotdata_check_io_compatibility( dev_id, stretch_disk->stretch_blk_dev, &is_compatible );
	if (res != SUCCESS){
		return res;
	}
	if (!is_compatible){
		log_errorln_dev_t( "Incompatible block size for device ", dev_id );
		return -EPERM;
	}

	snapdata = (snapshotdata_t*)content_new( &SnapshotDatas );
	if (snapdata == NULL){
		log_errorln( "Failed to allocate new snapshotdata" );
		return -ENOMEM;
	}

	do{
		snapdata->dev_id = dev_id;
		res = blk_dev_get_info( snapdata->dev_id, &info_dev );
		if (res != SUCCESS){
			log_errorln_dev_t( "Cannot obtain info about original device=.", dev_id );
			break;
		}

		res = __snapshotdata_create_common( snapdata, info_dev.count_sect );
		if (res != SUCCESS){
			break;
		}
#ifdef SNAPDATA_ZEROED
		if (get_zerosnapdata( )){
			if (snapdata->dev_id == stretch_disk->stretch_blk_dev_id){//snapshot data on same partition as snapshot

				rangevector_t* rangevec = &stretch_disk->stretch_dataranges;
				range_t* prange;

				log_traceln_dev_t( "Zeroing ranges set for device ", snapdata->dev_id );
				RANGEVECTOR_READ_LOCK( rangevec );
				RANGEVECTOR_FOREACH_BEGIN( rangevec, prange )
				{
					res = rangevector_add( &snapdata->zero_sectors, *prange );
					if (res != SUCCESS)
						break;
					log_traceln_range( "Zeroing range:", (*prange) );
				}
				RANGEVECTOR_FOREACH_END( rangevec );
				RANGEVECTOR_READ_UNLOCK( rangevec );

				if (res != SUCCESS){
					log_errorln_d( "Failed to set zero sectors. errno=", 0 - res );
					break;
				}
				rangevector_sort( &snapdata->zero_sectors );
			}
			else{
				log_traceln_dev_t( "Haven`t zeroing ranges.", snapdata->dev_id );
			}
		}
#endif
	} while (false);
	if (res == SUCCESS){
		snapdata->type = SNAPSHOTDATA_TYPE_STRETCH;
		snapdata->stretch = snapshotdata_stretch_disk_get_resource( stretch_disk );

		container_push_back( &SnapshotDatas, &snapdata->content );
	}
	else{
		if (snapdata != NULL){
			content_free( &snapdata->content );
		}
	}
	return res;
}

int snapshotdata_stretch_add_range( snapshotdata_stretch_disk_t* stretch_disk, range_t* range )
{
	int res = rangevector_add( &stretch_disk->stretch_dataranges, *range );
	if (res == SUCCESS){
		snapshotdata_blk_info_t* blk_info = &stretch_disk->stretch_blk_info;

		spin_lock( &blk_info->locker );
		blk_info->cnt += range->cnt;
		spin_unlock( &blk_info->locker );

		stretch_disk->halffilled = false;
	}

	return res;
}

void snapshotdata_stretch_halffill( snapshotdata_stretch_disk_t* stretch_disk, ssize_t fill_status )
{
	if (!stretch_disk->halffilled){
		ctrl_pipe_request_halffill( stretch_disk->ctrl_pipe, fill_status );

		stretch_disk->halffilled = true;
	}
}

void snapshotdata_stretch_overflow( snapshotdata_stretch_disk_t* stretch_disk, unsigned int error_code )
{
	if (!stretch_disk->overflowed){
		ctrl_pipe_request_overflow( stretch_disk->ctrl_pipe, error_code );

		stretch_disk->overflowed = true;
	}
}

void snapshotdata_stretch_terminate( snapshotdata_stretch_disk_t* stretch_disk )
{
	ctrl_pipe_request_terminate( stretch_disk->ctrl_pipe );
}

//////////////////////////////////////////////////////////////////////////
void snapshotdata_print_state( snapshotdata_t* snapshotdata )
{
	unsigned long wrote_mb;

	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );

	pr_warn( "sectors: copy_write=%lld \n",
		(long long int)atomic64_read( &snapshotdata->state_sectors_write ) );

	wrote_mb = (unsigned long)(atomic64_read( &snapshotdata->state_sectors_write ) >> (20 - SECTOR512_SHIFT));

	pr_warn( "bytes: copy_write=%lu MiB \n", wrote_mb );

	if (snapshotdata->corrupted){
		pr_warn( "Corrupted. Failed request count: %d MiB \n", atomic_read( &snapshotdata->corrupted_cnt ) );
	}
}

//////////////////////////////////////////////////////////////////////////

int __snapshotdata_check_io_compatibility( dev_t dev_id, struct block_device*  snapdata_blk_dev, bool* is_compatibility )
{
	int res = SUCCESS;
#if 0
	*is_compatibility = true;
	log_traceln_dev_t( "Compatible always for ", dev_id );
#else
	blk_dev_info_t dev_info;
	blk_dev_info_t snapdata_dev_info;

	*is_compatibility = false;

	res = blk_dev_get_info( dev_id, &dev_info );
	if (SUCCESS == res){
		res = __blk_dev_get_info( snapdata_blk_dev, &snapdata_dev_info );
		if (SUCCESS == res){

			if (snapdata_dev_info.logical_block_size <= dev_info.logical_block_size){
				log_traceln_dev_t( "Compatible snapshot data device and ", dev_id );

				//log_traceln_d( "device logical block size ", dev_info.logical_block_size );
				//log_traceln_d( "device physical block size ", dev_info.physical_block_size );
				//log_traceln_d( "device io_min ", dev_info.io_min );

				//log_traceln_d( "snapshot device logical block size ", snapdata_dev_info.logical_block_size );
				//log_traceln_d( "snapshot device physical block size ", snapdata_dev_info.physical_block_size );
				//log_traceln_d( "snapshot device io_min ", snapdata_dev_info.io_min );

				*is_compatibility = true;
			}
			else{
				log_errorln_dev_t( "Incompatible snapshot data device and ", dev_id );

				log_errorln_d( "device logical block size ", dev_info.logical_block_size );
				//log_errorln_d( "device physical block size ", dev_info.physical_block_size );
				//log_errorln_d( "device io_min ", dev_info.io_min );

				log_errorln_d( "snapshot device logical block size ", snapdata_dev_info.logical_block_size );
				//log_errorln_d( "snapshot device physical block size ", snapdata_dev_info.physical_block_size );
				//log_errorln_d( "snapshot device io_min ", snapdata_dev_info.io_min );
				*is_compatibility = false;
			}
		}
	}

	if (SUCCESS != res){
		log_errorln_dev_t( "Failed to check io compatibility for ", dev_id );
	}
#endif
	return res;
}
///
