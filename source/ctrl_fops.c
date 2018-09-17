#include "stdafx.h"
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>

#include "veeamsnap_ioctl.h"
#include "version.h"
#include "ctrl_pipe.h"
#include "tracking.h"
#include "snapshot.h"

#ifdef SNAPSTORE
#include "snapstore.h"
#else //SNAPSTORE
#include "snapshotdata.h"
#include "snapshotdata_memory.h"
#endif //SNAPSTORE

#include "snapdata_collect.h"
#include "snapimage.h"
#include "tracker.h"
#include "page_array.h"
#include "blk_deferred.h"




static atomic_t g_dev_open_cnt = ATOMIC_INIT( 0 );

static struct ioctl_getversion_s version = {
	.major		= FILEVER_MAJOR,
	.minor		= FILEVER_MINOR,
	.revision	= FILEVER_REVISION,
	.build		= FILEVER_BUILD
};


void ctrl_init( void )
{
	ctrl_pipe_init( );
}

void ctrl_done( void )
{
	ctrl_pipe_done( );
}

ssize_t ctrl_read(struct file *fl, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t bytes_read = 0;
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)fl->private_data;

	bytes_read = ctrl_pipe_read( pipe, buffer, length );
	if (bytes_read == 0)
		if (fl->f_flags & O_NONBLOCK)
			bytes_read = -EAGAIN;

	return bytes_read;
}


ssize_t ctrl_write( struct file *fl, const char __user *buffer, size_t length, loff_t *offset )
{
	ssize_t bytes_wrote = 0;
	char* kern_buffer = NULL;

	kern_buffer = dbg_kmalloc( length, GFP_KERNEL );
	if (kern_buffer == NULL){
		log_errorln_sz( "Failed to allocate buffer. length=", length );
		return -ENOMEM;
	}

	do{
		ctrl_pipe_t* pipe = (ctrl_pipe_t*)fl->private_data;

		if (0 != copy_from_user( kern_buffer, buffer, length )){
		log_errorln( "Invalid user buffer" );
			bytes_wrote = -EINVAL;

			break;
		}

		bytes_wrote = ctrl_pipe_write( pipe, kern_buffer, length );

	} while (false);
	dbg_kfree( kern_buffer );

	return bytes_wrote;
}


unsigned int ctrl_poll( struct file *fl, struct poll_table_struct *wait )
{
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)fl->private_data;

	return ctrl_pipe_poll( pipe );
}


int ctrl_open(struct inode *inode, struct file *fl)
{
	fl->f_pos = 0;

	try_module_get( THIS_MODULE );

	log_traceln_p( "file=", fl );

	atomic_inc( &g_dev_open_cnt );

	fl->private_data = (void*)ctrl_pipe_get_resource( ctrl_pipe_new( ) );
	if (fl->private_data == NULL){
		log_errorln( "Failed to create pipe" );
		return -ENOMEM;
	}

	return SUCCESS;
}


int ctrl_release(struct inode *inode, struct file *fl)
{
	int result = SUCCESS;

	log_traceln_p( "file=", fl );

	if ( atomic_read( &g_dev_open_cnt ) > 0 ){
		module_put( THIS_MODULE );
		atomic_dec( &g_dev_open_cnt );

		ctrl_pipe_put_resource( (ctrl_pipe_t*)fl->private_data );
	}
	else{
		log_errorln( "ioctl file isn`t closed." );
		result = -EALREADY;
	}

	return result;
}


int ioctl_compatibility_flags( unsigned long arg )
{
	struct ioctl_compatibility_flags_s param;

	log_traceln( "Get compatibility flags" );

	param.flags = 0;
#ifdef SNAPSTORE
	param.flags |= VEEAMSNAP_COMPATIBILITY_SNAPSTORE;
#endif

	if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_compatibility_flags_s ) )){
		log_errorln( "Invalid user buffer" );
		return -EINVAL;
	}

	return SUCCESS;
}

int ioctl_get_version( unsigned long arg )
{
	log_traceln( "Get version" );

	if (!access_ok( VERIFY_WRITE, (void*)arg, sizeof( struct ioctl_getversion_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_to_user( (void*)arg, &version, sizeof( struct ioctl_getversion_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return SUCCESS;
}

int ioctl_tracking_add( unsigned long arg )
{
	struct ioctl_dev_id_s dev;

	if (!access_ok( VERIFY_READ, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &dev, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return tracking_add( MKDEV( dev.major, dev.minor ), CBT_BLOCK_SIZE_DEGREE );
}

int ioctl_tracking_remove( unsigned long arg )
{
	struct ioctl_dev_id_s dev;

	log_traceln( "Removing from tracking device:" );

	if (!access_ok( VERIFY_READ, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &dev, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}
	log_traceln_d( "\tmajor=", dev.major );
	log_traceln_d( "\tminor=", dev.minor );

	return tracking_remove( MKDEV( dev.major, dev.minor ) );;
}

int ioctl_tracking_collect( unsigned long arg )
{
	int res;
	struct ioctl_tracking_collect_s get;
	struct cbt_info_s* p_cbt_info = NULL;

	log_traceln( "Collecting tracking device:" );

	if (!access_ok( VERIFY_WRITE, (void*)arg, sizeof( struct ioctl_tracking_collect_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &get, (void*)arg, sizeof( struct ioctl_tracking_collect_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	if (!access_ok( VERIFY_WRITE, get.p_cbt_info, get.count * sizeof( struct cbt_info_s ) )){
		log_errorln( "Invalid second buffer" );
		return -EINVAL;
	}

	p_cbt_info = dbg_kzalloc( get.count * sizeof( struct cbt_info_s ), GFP_KERNEL );
	if (NULL == p_cbt_info){
		log_errorln( "Cannot allocate memory" );
		return -ENOMEM;
	}

	do{
		res = tracking_collect( get.count, p_cbt_info, &get.count );
		if (SUCCESS != res){
			log_errorln( "Failed to execute tracking_collect" );
			break;
		}
		if (0 != copy_to_user( get.p_cbt_info, p_cbt_info, get.count*sizeof( struct cbt_info_s ) )){
			log_errorln( "Invalid user buffer for cbt info" );
			res = -ENODATA;
			break;
		}

		if (0 != copy_to_user( (void*)arg, (void*)&get, sizeof( struct ioctl_tracking_collect_s ) )){
			log_errorln( "Invalid user buffer for arguments" );
			res = -ENODATA;
			break;
		}

	} while (false);

	dbg_kfree( p_cbt_info );
	p_cbt_info = NULL;

	return res;
}

int ioctl_tracking_block_size( unsigned long arg )
{
	unsigned int blk_sz = CBT_BLOCK_SIZE;

	if (0 != copy_to_user( (void*)arg, &blk_sz, sizeof( unsigned int ) )){
		log_errorln( "Invalid user buffer for arguments" );
		return -ENODATA;
	}
	return SUCCESS;
}

int ioctl_tracking_read_cbt_map( unsigned long arg )
{
	struct ioctl_tracking_read_cbt_bitmap_s readbitmap;

	if (!access_ok( VERIFY_READ, (void*)arg, sizeof( struct ioctl_tracking_read_cbt_bitmap_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &readbitmap, (void*)arg, sizeof( struct ioctl_tracking_read_cbt_bitmap_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return tracking_read_cbt_bitmap(
		MKDEV( readbitmap.dev_id.major, readbitmap.dev_id.minor ),
		readbitmap.offset,
		readbitmap.length,
		(void*)readbitmap.buff
	);
}

int ioctl_snapshot_create( unsigned long arg )
{
	int status;
	struct ioctl_snapshot_create_s param;
	struct ioctl_dev_id_s* pk_dev_id = NULL;

	if (!access_ok( VERIFY_WRITE, (void*)arg, sizeof( struct ioctl_snapshot_create_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapshot_create_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	if (!access_ok( VERIFY_WRITE, param.p_dev_id, param.count*sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Cannot access to user buffer" );
		return -EINVAL;
	}

	pk_dev_id = dbg_kzalloc( sizeof( struct ioctl_dev_id_s ) * param.count, GFP_KERNEL );
	if (NULL == pk_dev_id){
		log_errorln( "Cannot allocate memory" );
		return -ENOMEM;
	}

	do{
		dev_t* p_dev = NULL;
		int inx = 0;

		if (0 != copy_from_user( pk_dev_id, (void*)param.p_dev_id, param.count*sizeof( struct ioctl_dev_id_s ) )){
			log_errorln( "Invalid user buffer from parameters." );
			status = -ENODATA;
			break;
		}

		p_dev = dbg_kzalloc( sizeof( dev_t ) * param.count, GFP_KERNEL );
		if (NULL == p_dev){
			log_errorln( "Cannot allocate memory" );
			status = -ENOMEM;
			break;
		}

		for (inx = 0; inx < param.count; ++inx)
			p_dev[inx] = MKDEV( pk_dev_id[inx].major, pk_dev_id[inx].minor );

		status = snapshot_Create( p_dev, param.count, CBT_BLOCK_SIZE, &param.snapshot_id );

		dbg_kfree( p_dev );
		p_dev = NULL;

	} while (false);
	dbg_kfree( pk_dev_id );
	pk_dev_id = NULL;

	if (status == SUCCESS){
		if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_snapshot_create_s ) )){
			log_errorln( "Invalid user buffer" );
			status = -ENODATA;
		}
	}

	return status;
}

int ioctl_snapshot_destroy( unsigned long arg )
{
	unsigned long long param;

	log_traceln( "Snapshot destroy" );

	if (!access_ok( VERIFY_READ, (void*)arg, sizeof( unsigned long long ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &param, (void*)arg, sizeof( unsigned long long ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return snapshot_Destroy( param );
}
//////////////////////////////////////////////////////////////////////////
#ifdef SNAPSTORE
//////////////////////////////////////////////////////////////////////////
int ioctl_snapstore_create( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_create_s param;
	struct ioctl_dev_id_s* pk_dev_id = NULL;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_create_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	pk_dev_id = dbg_kzalloc( sizeof( struct ioctl_dev_id_s ) * param.count, GFP_KERNEL );
	if (NULL == pk_dev_id){
		log_errorln( "Cannot allocate memory" );
		return -ENOMEM;
	}

	do{
		size_t inx = 0;
		dev_t* dev_id_set = NULL;
		uuid_t* id = (uuid_t*)param.id;
		dev_t snapstore_dev_id = MKDEV( param.snapstore_dev_id.major, param.snapstore_dev_id.minor );
		size_t dev_id_set_length = (size_t)param.count;


		if (0 != copy_from_user( pk_dev_id, (void*)param.p_dev_id, param.count*sizeof( struct ioctl_dev_id_s ) )){
			log_errorln( "Invalid user buffer from parameters." );
			res = -ENODATA;
			break;
		}

		dev_id_set = dbg_kzalloc( sizeof( dev_t ) * param.count, GFP_KERNEL );
		if (NULL == dev_id_set){
			log_errorln( "Cannot allocate memory" );
			res = -ENOMEM;
			break;
		}

		for (inx = 0; inx < dev_id_set_length; ++inx)
			dev_id_set[inx] = MKDEV( pk_dev_id[inx].major, pk_dev_id[inx].minor );

		res = snapstore_create( id, snapstore_dev_id, dev_id_set, dev_id_set_length );

		dbg_kfree( dev_id_set );
	} while (false);
	dbg_kfree( pk_dev_id );

	return res;
}
int ioctl_snapstore_file( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_file_add_s param;
	struct ioctl_range_s* ranges = NULL;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_file_add_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	ranges = dbg_kzalloc( sizeof( struct ioctl_range_s ) * param.range_count, GFP_KERNEL );
	if (NULL == ranges){
		log_errorln( "Cannot allocate memory" );
		return -ENOMEM;
	}
	do{
		uuid_t* id = (uuid_t*)(param.id);
		size_t ranges_cnt = (size_t)param.range_count;

		if (0 != copy_from_user( ranges, (void*)param.ranges, ranges_cnt*sizeof( struct ioctl_range_s ) )){
			log_errorln( "Invalid user buffer from parameters." );
			res = -ENODATA;
			break;
		}

		res = snapstore_add_file( id, ranges, ranges_cnt );
	}while (false);
	dbg_kfree( ranges );

	return res;
}
int ioctl_snapstore_memory( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_memory_limit_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_memory_limit_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	res = snapstore_add_memory( (uuid_t*)param.id, param.size );

	return res;
}
int ioctl_snapstore_cleanup( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapstore_cleanup_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapstore_cleanup_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}
	log_traceln_uuid( "id=", ((uuid_t*)(param.id)) );
	res = snapstore_cleanup( (uuid_t*)param.id, &param.filled_bytes );

	if (res == SUCCESS){
		if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_snapstore_cleanup_s ) )){
			log_errorln( "Invalid user buffer" );
			res = -ENODATA;
		}
	}

	return res;
}
//////////////////////////////////////////////////////////////////////////
#else //SNAPSTORE
//////////////////////////////////////////////////////////////////////////
int ioctl_snapshotdata_add_dev( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapshotdata_add_dev_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapshotdata_add_dev_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	{
		snapshotdata_shared_t* shared = NULL;
		shared = snapshotdata_shared_find_by_id( (uuid_t*)(param.id) );
		if (shared == NULL)
			return -ENODATA;

		return snapshotdata_add_dev( shared, MKDEV( param.dev_id.major, param.dev_id.minor ) );
	}

	return res;
}

int ioctl_snapshotdata_common( unsigned long arg )
{
	int res = SUCCESS;
	struct ioctl_snapshotdata_common_s param;
	dev_t dev_id_host_data;
	size_t range_count;
	uuid_t* id = NULL;
	struct ioctl_range_s* user_ranges = NULL;
	size_t buff_size;
	struct ioctl_range_s* local_range = NULL;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapshotdata_common_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	log_traceln_dev_id_s( "dev_id_host_data:", param.dev_id_host_data );
	log_traceln_d( "rangeset count=", param.range_count );

	id = (uuid_t*)(param.id);
	dev_id_host_data = MKDEV( param.dev_id_host_data.major, param.dev_id_host_data.minor );
	range_count = (size_t)param.range_count;
	user_ranges = param.ranges;

	buff_size = range_count * sizeof( struct ioctl_range_s );
	if (!access_ok( VERIFY_READ, (void*)user_ranges, buff_size )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	local_range = dbg_kzalloc( buff_size, GFP_KERNEL );
	if (NULL == local_range){
		log_errorln( "Cannot allocate buffer for ranges" );
		return -ENOMEM;
	}

	do{
		if (0 != copy_from_user( local_range, (void*)user_ranges, buff_size )){
			log_errorln( "Invalid user buffer" );
			res = -ENODATA;
			break;
		}

		{
			snapshotdata_common_t* comm_disk = NULL;

			comm_disk = snapshotdata_common_create( id, dev_id_host_data );
			if (comm_disk == NULL){
				res = -EFAULT;
				break;
			}

			res = snapshotdata_add_ranges( &comm_disk->file, &comm_disk->blkinfo, local_range, range_count );
			if (SUCCESS != res)
				break;
		}
	} while (false);

	dbg_kfree( local_range );

	if (res == SUCCESS){
		log_traceln( "success." );
	}else{
		log_errorln_d( "fail.", res );
	}
	return res;
}

int ioctl_snapshotdata_memory( unsigned long arg )
{
	struct ioctl_snapshotdata_memory_s param;
	size_t size;
	log_traceln( "Snapshot data make information for memory" );

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapshotdata_memory_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	size = (size_t)param.snapshotdatasize;
	//for ILP32
	if ((sizeof( size_t ) == 4) && (param.snapshotdatasize > 0x7FFFffffULL)){
		log_errorln_llx( "Snapshot data size too big to your system. it`s=", param.snapshotdatasize );
		return -EINVAL;
	}

	if (NULL == snapshotdata_memory_create( (uuid_t*)(param.id), size) )
		return -ENOMEM;

	return SUCCESS;
}

int ioctl_snapshotdata_clean( unsigned long arg )
{
	struct ioctl_snapshotdata_clean_s param;

	log_traceln( "Snapshot data clean information" );

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_snapshotdata_clean_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;;
	}
	return snapshotdata_shared_cleanup( (uuid_t*)(param.id) );
}
//////////////////////////////////////////////////////////////////////////
#endif //SNAPSTORE
//////////////////////////////////////////////////////////////////////////

int ioctl_snapshot_errno( unsigned long arg )
{
	int res;
	struct ioctl_snapshot_errno_s param;

	log_traceln( "Snapshot get errno for device");

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}
#ifdef SNAPSTORE
	res = snapstore_device_errno( MKDEV( param.dev_id.major, param.dev_id.minor ), &param.err_code );
#else
	res = snapshotdata_Errno( MKDEV( param.dev_id.major, param.dev_id.minor ), &param.err_code );
#endif
	if (res != SUCCESS)
		return res;

	if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_snapshot_errno_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	return SUCCESS;
}

int ioctl_collect_snapshotdata_location_start( unsigned long arg )
{
	struct ioctl_collect_snapshotdata_location_start_s param;

	log_traceln( "Collect snapshot data location start." );

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_collect_snapshotdata_location_start_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	return snapdata_collect_LocationStart(
		MKDEV( param.dev_id.major, param.dev_id.minor ),
		param.magic_buff,
		param.magic_length
		);
}

int ioctl_collect_snapshotdata_location_get( unsigned long arg )
{
	int res;
	struct ioctl_collect_snapshotdata_location_get_s param;
	rangelist_t ranges;
	size_t ranges_count = 0;

	log_traceln( "Collect snapshot data location get." );

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_collect_snapshotdata_location_get_s ) )){
		log_errorln( "Invalid input buffer" );
		return -EINVAL;
	}

	rangelist_init( &ranges );
	do{
		res = snapdata_collect_LocationGet( MKDEV( param.dev_id.major, param.dev_id.minor ), &ranges, &ranges_count );
		if (res != SUCCESS){
			log_errorln( "Cannot get location" );
			break;
		}
		log_traceln_sz( "ranges_count=", ranges_count );


		if (param.ranges == NULL ){
			log_traceln( "ranges parameter is null" );
			res = SUCCESS;
			break;
		}

		if (param.range_count < ranges_count){
			log_errorln( "Invalid range array count" );
			log_errorln_d( "ranges buffer available: ", param.range_count );
			log_errorln_sz( "ranges needed: ", ranges_count );
			res = -EINVAL;
			break;
		}

		{
			size_t inx = 0;
			range_t  rg;
			struct ioctl_range_s rg_ctl;

			for (inx = 0; (SUCCESS == rangelist_get( &ranges, &rg )) && (inx < ranges_count); ++inx){
				rg_ctl.left = sector_to_streamsize( rg.ofs );
				rg_ctl.right = rg_ctl.left + sector_to_streamsize( rg.cnt );

				if (0 != copy_to_user( param.ranges + inx, &rg_ctl, sizeof( struct ioctl_range_s ) )){
					log_errorln( "Invalid range array buffer" );
					res = -EINVAL;
					break;
				};
			}
		}
	} while (false);
	rangelist_done( &ranges );

	if (res == SUCCESS){
		param.range_count = ranges_count;
		if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_collect_snapshotdata_location_get_s ) )){
			log_errorln( "Invalid output buffer" );
			res = -EINVAL;
		}
	}

	return res;
}

int ioctl_collect_snapshotdata_location_complete( unsigned long arg )
{
	struct ioctl_collect_snapshotdata_location_complete_s param;

	log_traceln( "Collect snapshot data location stop." );

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_collect_snapshotdata_location_complete_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	return snapdata_collect_LocationComplete( MKDEV( param.dev_id.major, param.dev_id.minor ) );
}

/*

int ioctl_direct_device_read( unsigned long arg )
{
	int status = SUCCESS;
	struct ioctl_direct_device_read_s param;

	if (!access_ok( VERIFY_READ, (void*)arg, sizeof( struct ioctl_direct_device_read_s ) )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_direct_device_read_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	if (!access_ok( VERIFY_WRITE, (void*)param.buff, param.length )){
		log_errorln( "Invalid buffer" );
		return -EINVAL;
	}
	status = direct_device_read4user( MKDEV( param.dev_id.major, param.dev_id.minor ), param.offset, param.buff, param.length );

	return status;
}


int ioctl_direct_device_close( unsigned long arg )
{
	struct ioctl_dev_id_s dev_id_s;

	log_traceln(".");

	if (0 != copy_from_user( &dev_id_s, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return direct_device_close( MKDEV( dev_id_s.major, dev_id_s.minor ) );
}


int ioctl_direct_device_open( unsigned long arg )
{
	struct ioctl_dev_id_s dev_id_s;

	log_traceln( "." );

	if (0 != copy_from_user( &dev_id_s, (void*)arg, sizeof( struct ioctl_dev_id_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return direct_device_open( MKDEV( dev_id_s.major, dev_id_s.minor ) );
}


int ioctl_get_block_size( unsigned long arg )
{
	int status = SUCCESS;
	struct ioctl_get_block_size_s param;

	log_traceln( "." );
	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_get_block_size_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	{
		blk_dev_info_t info;
		status = blk_dev_get_info( MKDEV(param.dev_id.major, param.dev_id.minor), &info );
		if (status != SUCCESS)
			return status;
		param.block_size = info.blk_size;
	}

	if (status == SUCCESS){
		int left_data_length = copy_to_user( (void*)arg, &param, sizeof( struct ioctl_get_block_size_s ) );
		if (left_data_length != 0){
			log_errorln_d( "Cannot copy data to user buffer ", (int)left_data_length );
			return -ENODATA;
		}
	}

	return status;
}
*/


int ioctl_collect_snapimages( unsigned long arg )
{
	int status = SUCCESS;
	struct ioctl_collect_shapshot_images_s param;

	if (0 != copy_from_user( &param, (void*)arg, sizeof( struct ioctl_collect_shapshot_images_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	status = snapimage_collect_images( param.count, param.p_image_info, &param.count );

	if (0 != copy_to_user( (void*)arg, &param, sizeof( struct ioctl_collect_shapshot_images_s ) )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}

	return status;
}

int ioctl_printstate( unsigned long arg )
{
	pr_warn( "--------------------------------------------------------------------------\n" );
	pr_warn( "%s state:\n", MODULE_NAME );
	pr_warn( "version: %d.%d.%d.%d.\n", version.major, version.minor, version.revision, version.build );
#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
	dbg_mem_print_state( );
#endif
	snapimage_print_state( );
	tracker_print_state( );
	page_arrays_print_state( );
	blk_deferred_print_state( );
	pr_warn( "size of struct bio %lu bytes.\n", (unsigned long)sizeof( struct bio ) );
	pr_warn( "--------------------------------------------------------------------------\n" );

	return SUCCESS;
}


typedef int (veeam_ioctl_t)(unsigned long arg);
typedef struct veeam_ioctl_table_s{
	unsigned int cmd;
	veeam_ioctl_t* fn;
	char* name;
}veeam_ioctl_table_t;

static veeam_ioctl_table_t veeam_ioctl_table[] =
{
	{ (IOCTL_COMPATIBILITY_FLAGS), ioctl_compatibility_flags, "IOCTL_COMPATIBILITY_FLAGS" },
	{ (IOCTL_GETVERSION), ioctl_get_version, "IOCTL_GETVERSION" },

	{ (IOCTL_TRACKING_ADD), ioctl_tracking_add, "IOCTL_TRACKING_ADD" },
	{ (IOCTL_TRACKING_REMOVE), ioctl_tracking_remove, "IOCTL_TRACKING_REMOVE" },
	{ (IOCTL_TRACKING_COLLECT), ioctl_tracking_collect, "IOCTL_TRACKING_COLLECT" },
	{ (IOCTL_TRACKING_BLOCK_SIZE), ioctl_tracking_block_size, "IOCTL_TRACKING_BLOCK_SIZE" },
	{ (IOCTL_TRACKING_READ_CBT_BITMAP), ioctl_tracking_read_cbt_map, "IOCTL_TRACKING_READ_CBT_BITMAP" },

	{ (IOCTL_SNAPSHOT_CREATE), ioctl_snapshot_create, "IOCTL_SNAPSHOT_CREATE" },
	{ (IOCTL_SNAPSHOT_DESTROY), ioctl_snapshot_destroy, "IOCTL_SNAPSHOT_DESTROY" },
	{ (IOCTL_SNAPSHOT_ERRNO), ioctl_snapshot_errno, "IOCTL_SNAPSHOT_ERRNO" },

#ifdef SNAPSTORE
	{ (IOCTL_SNAPSTORE_CREATE), ioctl_snapstore_create, "IOCTL_SNAPSTORE_CREATE" },
	{ (IOCTL_SNAPSTORE_FILE), ioctl_snapstore_file, "IOCTL_SNAPSTORE_FILE" },
	{ (IOCTL_SNAPSTORE_MEMORY), ioctl_snapstore_memory, "IOCTL_SNAPSTORE_MEMORY" },
	{ (IOCTL_SNAPSTORE_CLEANUP), ioctl_snapstore_cleanup, "IOCTL_SNAPSTORE_CLEANUP" },
#else
	{ (IOCTL_SNAPSHOTDATA_MEMORY), ioctl_snapshotdata_memory, "IOCTL_SNAPSHOTDATA_MEMORY" },
	{ (IOCTL_SNAPSHOTDATA_COMMON), ioctl_snapshotdata_common, "IOCTL_SNAPSHOTDATA_COMMON" },
	{ (IOCTL_SNAPSHOTDATA_CLEAN), ioctl_snapshotdata_clean, "IOCTL_SNAPSHOTDATA_CLEAN" },
	{ (IOCTL_SNAPSHOTDATA_ADD_DEV), ioctl_snapshotdata_add_dev, "IOCTL_SNAPSHOTDATA_ADD_DEV" },
#endif

	//{ (IOCTL_DIRECT_DEVICE_READ), ioctl_direct_device_read, "IOCTL_DIRECT_DEVICE_READ" },
	//{ (IOCTL_DIRECT_DEVICE_OPEN), ioctl_direct_device_open, "IOCTL_DIRECT_DEVICE_OPEN" },
	//{ (IOCTL_DIRECT_DEVICE_CLOSE), ioctl_direct_device_close, "IOCTL_DIRECT_DEVICE_CLOSE" },
	//{ (IOCTL_GET_BLOCK_SIZE), ioctl_get_block_size, "IOCTL_GET_BLOCK_SIZE" },

	{ (IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_START), ioctl_collect_snapshotdata_location_start, "IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_START" },
	{ (IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_GET), ioctl_collect_snapshotdata_location_get, "IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_GET" },
	{ (IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_COMPLETE), ioctl_collect_snapshotdata_location_complete, "IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_COMPLETE" },
	{ (IOCTL_COLLECT_SNAPSHOT_IMAGES), ioctl_collect_snapimages, "IOCTL_COLLECT_SNAPSHOT_IMAGES" },

	{ (IOCTL_PRINTSTATE), ioctl_printstate, "IOCTL_PRINTSTATE" },
	{ 0, NULL, NULL}
};


long ctrl_unlocked_ioctl( struct file *filp, unsigned int cmd, unsigned long arg )
{
	long status = -ENOTTY;
	size_t inx = 0;
	//if (unlikely( cmd == IOCTL_DIRECT_DEVICE_READ )){
	//	return ioctl_direct_device_read( arg );
	//}
	while (veeam_ioctl_table[inx].cmd != 0){
		if (veeam_ioctl_table[inx].cmd == cmd){
#ifdef VEEAM_IOCTL_LOGGING
			if (veeam_ioctl_table[inx].name != NULL){
				log_warnln( veeam_ioctl_table[inx].name );
			}
#endif
			status = veeam_ioctl_table[inx].fn( arg );
			break;
		}
		++inx;
	}

	return status;
}

