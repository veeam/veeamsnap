#include "stdafx.h"
#include "version.h"

#include "container.h"
#include "container_spinlocking.h"
#include "range.h"
#include "rangeset.h"
#include "rangelist.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"
#include "shared_resource.h"
#include "ctrl_fops.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"
#include "defer_io.h"
#include "tracker_queue.h"
#include "snapshot.h"
#include "cbt_map.h"
#include "veeamsnap_ioctl.h"
#include "tracker.h"
#include "tracking.h"
#include "sparse_bitmap.h"
#include "snapdata_collect.h"
#include "snapimage.h"
#include "direct_device.h"
#include "page_array.h"
//////////////////////////////////////////////////////////////////////////
// global module parameters
//////////////////////////////////////////////////////////////////////////
int g_param_zerosnapdata = 0;
int g_param_debuglogging = 0;

int get_debuglogging( void )
{
	return g_param_debuglogging;
}
int get_zerosnapdata( void )
{
	return g_param_zerosnapdata;
}
//////////////////////////////////////////////////////////////////////////

static int veeamsnap_major = 0;

//////////////////////////////////////////////////////////////////////////
static struct file_operations ctrl_fops = {
	.owner  = THIS_MODULE,
	.read   = ctrl_read,
	.write  = ctrl_write,
	.open   = ctrl_open,
	.release= ctrl_release,
	.poll   = ctrl_poll,
	//.ioctl  = ctrl_ioctl,
	.unlocked_ioctl = ctrl_unlocked_ioctl
};
//////////////////////////////////////////////////////////////////////////
int __init veeamsnap_init(void)
{
	int conteiner_cnt = 0;
	int result = SUCCESS;

	log_traceln("Loading");
	log_traceln_s( "Version: ", FILEVER_STR );
	log_traceln_s( "Author: ", AUTHOR_STR );
	log_traceln_s( "licence: ", LICENCE_STR );
	log_traceln_s( "description: ", DESCRIPTION_STR );

	log_traceln_d( "zerosnapdata: ", g_param_zerosnapdata );
	log_traceln_d( "debuglogging: ", g_param_debuglogging );

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "start. container_alloc_counter=", conteiner_cnt );

	log_traceln_d( "start. mem_cnt=", atomic_read( &mem_cnt) );
	log_traceln_d( "start. vmem_cnt=", atomic_read(&vmem_cnt) );

	dbg_mem_init( );
	page_arrays_init( );

	do{
		ctrl_init( );

		veeamsnap_major = register_chrdev(0, MODULE_NAME, &ctrl_fops);
		if (veeamsnap_major < 0) {
			log_errorln_d("Registering the character device failed with ", veeamsnap_major);
			result = veeamsnap_major;
			break;
		}
		log_traceln_d ("Module major=", veeamsnap_major);

		if ((result = blk_bioset_create( )) != SUCCESS)
			break;

		dio_init( );
		if ((result = dio_bioset_create( )) != SUCCESS)
			break;

		if ((result = sparsebitmap_init( )) != SUCCESS)
			break;

		if ((result = tracker_Init( )) != SUCCESS)
			break;

		if ((result = tracker_queue_Init( )) != SUCCESS)
			break;

		tracking_Init( );

		if ((result = snapshot_Init( )) != SUCCESS)
			break;

		if ((result = snapshotdata_Init( )) != SUCCESS)
			break;

		if ((result = snapshotdata_common_Init( )) != SUCCESS)
			break;

		if ((result = snapshotdata_stretch_Init( )) != SUCCESS)
			break;

		if ((result = snapdata_collect_Init( )) != SUCCESS)
			break;

		if ((result = snapimage_init( )) != SUCCESS)
			break;

		if ((result = direct_device_init( )) != SUCCESS)
			break;
	}while(false);

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "end. container_alloc_counter=", conteiner_cnt );

	conteiner_cnt = container_sl_alloc_counter( );
	log_traceln_d( "start. container_sl_alloc_counter=", conteiner_cnt );

	log_traceln_d( "end. mem_cnt=", atomic_read( &mem_cnt ) );
	log_traceln_d( "end. vmem_cnt=", atomic_read( &vmem_cnt ) );

	return result;
}

void __exit veeamsnap_exit(void)
{
	int conteiner_cnt = 0;
	int result;
	log_traceln("Unloading module");

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "start. container_alloc_counter=", conteiner_cnt );

	conteiner_cnt = container_sl_alloc_counter( );
	log_traceln_d( "start. container_sl_alloc_counter=", conteiner_cnt );

	log_traceln_d( "start. mem_cnt=", atomic_read( &mem_cnt ) );
	log_traceln_d( "start. vmem_cnt=", atomic_read( &vmem_cnt ) );

	log_traceln_sz( "vmem max usage=", dbg_vmem_get_max_usage( ) );

	result = snapshot_Done( );
	if (SUCCESS == result){

		direct_device_done( );

		snapdata_collect_Done( );

		snapshotdata_Done( );

		snapshotdata_common_Done( );

		snapshotdata_stretch_Done( );

		result = tracking_Done( );
		if (SUCCESS == result){
			result = tracker_Done( );
			if (SUCCESS == result){
				result = tracker_queue_Done( );
			}
		}
		snapimage_done( );

		sparsebitmap_done( );

		dio_bioset_free( );

		blk_bioset_free( );
	}

	if (SUCCESS != result){
		log_traceln_d( "Unloading fail. err=", (0 - result) );
		return;
	}

	unregister_chrdev(veeamsnap_major, MODULE_NAME);

	ctrl_done( );

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "end. container_alloc_counter=", conteiner_cnt );
	log_traceln_d( "end. mem_cnt=", atomic_read( &mem_cnt ) );
	log_traceln_d( "end. vmem_cnt=", atomic_read( &vmem_cnt ) );
	log_traceln_d("Module unloaded. Major=",veeamsnap_major);

}

module_init(veeamsnap_init);
module_exit(veeamsnap_exit);

//////////////////////////////////////////////////////////////////////////////

module_param_named( zerosnapdata, g_param_zerosnapdata, int, 0644 );
MODULE_PARM_DESC( zerosnapdata, "Zeroing snapshot data algorithm determine." );

module_param_named( debuglogging, g_param_debuglogging, int, 0644 );
MODULE_PARM_DESC( debuglogging, "Logging level switch." );

MODULE_LICENSE( LICENCE_STR );
MODULE_AUTHOR( AUTHOR_STR );

MODULE_DESCRIPTION( DESCRIPTION_STR );
MODULE_VERSION(FILEVER_STR);
