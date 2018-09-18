#include "stdafx.h"
#include "version.h"
#include "veeamsnap_ioctl.h"
#include "ctrl_fops.h"
#include "ctrl_pipe.h"

#include "blk_direct.h"
#include "blk_redirect.h"
#include "blk_deferred.h"
#include "snapimage.h"
#include "snapdata_collect.h"

#ifdef SNAPSTORE
#include "snapstore.h"
#include "snapstore_device.h"
#else //SNAPSTORE
#include "snapshotdata_stretch.h"
#include "snapshotdata_common.h"
#include "snapshotdata_memory.h"
#include "snapshotdata.h"
#endif //SNAPSTORE

#include "snapshot.h"
#include "tracker_queue.h"
#include "tracker.h"
#include "sparse_bitmap.h"


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

static int veeamsnap_major = 0;


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

static inline void show_distrib(const char* distrib_name)
{
    pr_warn("Compile for distributive: %s", distrib_name);
}

static inline void show_distrib_version(const char* distrib_name)
{
#if defined(DISTRIB_VERSION_1) && defined(DISTRIB_VERSION_2)
    pr_warn("Compile for distributive: %s %d.%d", distrib_name, DISTRIB_VERSION_1, DISTRIB_VERSION_2);
#else
#if defined(DISTRIB_VERSION_1)
    pr_warn("Compile for distributive: %s %d", distrib_name, DISTRIB_VERSION_1);
#else
    show_distrib(distrib_name);
#endif
#endif
}

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

#ifdef SNAPSTORE
	log_traceln( "snapstore enabled" );
#endif

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "start. container_alloc_counter=", conteiner_cnt );

	conteiner_cnt = container_sl_alloc_counter( );
	log_traceln_d( "start. container_sl_alloc_counter=", conteiner_cnt );

#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
	log_traceln_d( "start. mem_cnt=", atomic_read( &g_mem_cnt) );
	log_traceln_d( "start. vmem_cnt=", atomic_read(&g_vmem_cnt) );
	dbg_mem_init( );
#endif


#if defined(DISTRIB_NAME_RHEL) || defined(DISTRIB_NAME_CENTOS) 
    show_distrib("RHEL or CentOS");
#endif
#if defined(DISTRIB_NAME_FEDORA)
    show_distrib_version("Fedora");
#endif
#if defined(DISTRIB_NAME_SLES) || defined(DISTRIB_NAME_SLES_SAP)
    show_distrib_version("SLES");
#endif
#if defined(DISTRIB_NAME_OPENSUSE) || defined(DISTRIB_NAME_OPENSUSE_LEAP)
    show_distrib_version("OpenSUSE");
#endif
#if defined(DISTRIB_NAME_OPENSUSE_TUMBLEWEED)
    show_distrib_version("OpenSUSE Tumbleweed");
#endif
#if defined(DISTRIB_NAME_DEBIAN)
    show_distrib_version("Debian");
#endif
#if defined(DISTRIB_NAME_UBUNTU)
    show_distrib_version("Ubuntu");
#endif



	//btreefs_enum( );

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

		if ((result = blk_direct_bioset_create( )) != SUCCESS)
			break;
		if ((result = blk_redirect_bioset_create( )) != SUCCESS)
			break;

		blk_deferred_init( );
		if ((result = blk_deferred_bioset_create( )) != SUCCESS)
			break;

		if ((result = sparsebitmap_init( )) != SUCCESS)
			break;

		if ((result = tracker_Init( )) != SUCCESS)
			break;

		if ((result = tracker_queue_Init( )) != SUCCESS)
			break;

		if ((result = snapshot_Init( )) != SUCCESS)
			break;

#ifdef SNAPSTORE
		if ((result = snapstore_device_init( )) != SUCCESS)
			break;
		if ((result = snapstore_init( )) != SUCCESS)
			break;
#else
		if ((result = snapshotdata_Init( )) != SUCCESS)
			break;
		if ((result = snapshotdata_memory_Init( )) != SUCCESS)
			break;
		if ((result = snapshotdata_common_Init( )) != SUCCESS)
			break;
		if ((result = snapshotdata_stretch_Init( )) != SUCCESS)
			break;
#endif

		if ((result = snapdata_collect_Init( )) != SUCCESS)
			break;

		if ((result = snapimage_init( )) != SUCCESS)
			break;

	}while(false);

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "end. container_alloc_counter=", conteiner_cnt );

	conteiner_cnt = container_sl_alloc_counter( );
	log_traceln_d( "end. container_sl_alloc_counter=", conteiner_cnt );
#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
	log_traceln_d( "end. mem_cnt=", atomic_read( &g_mem_cnt ) );
	log_traceln_d( "end. vmem_cnt=", atomic_read( &g_vmem_cnt ) );
#endif
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
#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
	log_traceln_d( "start. mem_cnt=", atomic_read( &g_mem_cnt ) );
	log_traceln_d( "start. vmem_cnt=", atomic_read( &g_vmem_cnt ) );

	log_traceln_sz( "vmem max usage=", dbg_vmem_get_max_usage( ) );
#endif
	result = snapshot_Done( );
	if (SUCCESS == result){

		snapdata_collect_Done( );

#ifdef SNAPSTORE
		snapstore_device_done( );
		snapstore_done( );
#else
		snapshotdata_Done( );
		snapshotdata_memory_Done( );
		snapshotdata_common_Done( );
		snapshotdata_stretch_Done( );
#endif //SNAPSTORE

		result = tracker_Done( );
		if (SUCCESS == result){
			result = tracker_queue_Done( );
		}

		snapimage_done( );

		sparsebitmap_done( );



		blk_deferred_bioset_free( );
		blk_deferred_done( );

		blk_redirect_bioset_free( );
		blk_direct_bioset_free( );
	}

	if (SUCCESS != result){
		log_traceln_d( "Unloading fail. err=", result );
		return;
	}

	unregister_chrdev(veeamsnap_major, MODULE_NAME);

	ctrl_done( );

	conteiner_cnt = container_alloc_counter( );
	log_traceln_d( "end. container_alloc_counter=", conteiner_cnt );

	conteiner_cnt = container_sl_alloc_counter( );
	log_traceln_d( "end. container_sl_alloc_counter=", conteiner_cnt );
#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
	log_traceln_d( "end. mem_cnt=", atomic_read( &g_mem_cnt ) );
	log_traceln_d( "end. vmem_cnt=", atomic_read( &g_vmem_cnt ) );
#endif
	log_traceln_d("Module unloaded. Major=",veeamsnap_major);

}

module_init(veeamsnap_init);
module_exit(veeamsnap_exit);


module_param_named( zerosnapdata, g_param_zerosnapdata, int, 0644 );
MODULE_PARM_DESC( zerosnapdata, "Zeroing snapshot data algorithm determine." );

module_param_named( debuglogging, g_param_debuglogging, int, 0644 );
MODULE_PARM_DESC( debuglogging, "Logging level switch." );

MODULE_LICENSE( LICENCE_STR );
MODULE_AUTHOR( AUTHOR_STR );

MODULE_DESCRIPTION( DESCRIPTION_STR );
MODULE_VERSION(FILEVER_STR);

MODULE_INFO( supported, "external" );
