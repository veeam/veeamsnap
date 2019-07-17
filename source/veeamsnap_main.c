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

#include "snapstore.h"
#include "snapstore_device.h"

#include "snapshot.h"
#include "tracker_queue.h"
#include "tracker.h"
#include "sparse_bitmap.h"

//#include "btrfs_support.h"

#define SECTION "main      "
#include "log_format.h"

static int g_param_zerosnapdata = 0;
static int g_param_debuglogging = 0;
static char* logdir = "/var/log/veeam";

static int g_param_snapstore_block_size_pow = 14;
static int g_param_change_tracking_block_size_pow = 18;
static unsigned int g_param_fixflags = 0;

int get_debuglogging( void )
{
    return g_param_debuglogging;
}
int get_zerosnapdata( void )
{
    return g_param_zerosnapdata;
}
int get_snapstore_block_size_pow(void)
{
    return g_param_snapstore_block_size_pow;
}
int inc_snapstore_block_size_pow(void)
{
    if (g_param_snapstore_block_size_pow > 30)
        return -EFAULT;

    ++g_param_snapstore_block_size_pow;
    return SUCCESS;
}
int get_change_tracking_block_size_pow(void)
{
    return g_param_change_tracking_block_size_pow;
}

unsigned int get_fixflags(void)
{
    return g_param_fixflags;
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
    log_tr_format("Compile for distributive: %s", distrib_name);
}

static inline void show_distrib_version(const char* distrib_name)
{
#if defined(DISTRIB_VERSION_1) && defined(DISTRIB_VERSION_2)
    log_tr_format("Compile for distributive: %s %d.%d", distrib_name, DISTRIB_VERSION_1, DISTRIB_VERSION_2);
#else
#if defined(DISTRIB_VERSION_1)
    log_tr_format("Compile for distributive: %s %d", distrib_name, DISTRIB_VERSION_1);
#else
    show_distrib(distrib_name);
#endif
#endif
}

int __init veeamsnap_init(void)
{
    //int conteiner_cnt = 0;
    int result = SUCCESS;

#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
    dbg_mem_init( );
#endif
    logging_init( logdir );
    log_tr( "================================================================================" );
    log_tr( "Loading" );
    log_tr_s( "Version: ", FILEVER_STR );
    log_tr_s( "Author: ", AUTHOR_STR );
    log_tr_s( "licence: ", LICENCE_STR );
    log_tr_s( "description: ", DESCRIPTION_STR );

    log_tr_d( "zerosnapdata: ", g_param_zerosnapdata );
    log_tr_d( "debuglogging: ", g_param_debuglogging );
    log_tr_d("snapstore_block_size_pow: ", g_param_snapstore_block_size_pow);
    log_tr_d("change_tracking_block_size_pow: ", g_param_change_tracking_block_size_pow);
    log_tr_s( "logdir: ", logdir );
    log_tr_x("fixflags: ", g_param_fixflags);

    if (g_param_snapstore_block_size_pow > 23){
        g_param_snapstore_block_size_pow = 23;
        log_tr_d("Limited snapstore_block_size_pow: ", g_param_snapstore_block_size_pow);
    }
    else if (g_param_snapstore_block_size_pow < 12){
        g_param_snapstore_block_size_pow = 12;
        log_tr_d("Limited snapstore_block_size_pow: ", g_param_snapstore_block_size_pow);
    }

    if (g_param_change_tracking_block_size_pow > 23){
        g_param_change_tracking_block_size_pow = 23;
        log_tr_d("Limited change_tracking_block_size_pow: ", g_param_change_tracking_block_size_pow);
    }
    else if (g_param_change_tracking_block_size_pow < 12){
        g_param_change_tracking_block_size_pow = 12;
        log_tr_d("Limited change_tracking_block_size_pow: ", g_param_change_tracking_block_size_pow);
    }

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

#ifdef SNAPIMAGE_TRACER
    log_tr("Snapshot image tracing is available");
#endif

    //btreefs_enum( );

    page_arrays_init( );

    do{
        ctrl_init( );

        veeamsnap_major = register_chrdev(0, MODULE_NAME, &ctrl_fops);
        if (veeamsnap_major < 0) {
            log_err_d("Failed to register a character device. errno=", veeamsnap_major);
            result = veeamsnap_major;
            break;
        }
        log_tr_format("Module major [%d]", veeamsnap_major);

        if ((result = blk_direct_bioset_create( )) != SUCCESS)
            break;
        if ((result = blk_redirect_bioset_create( )) != SUCCESS)
            break;

        blk_deferred_init( );
        if ((result = blk_deferred_bioset_create( )) != SUCCESS)
            break;

        if ((result = sparsebitmap_init( )) != SUCCESS)
            break;

        if ((result = tracker_init( )) != SUCCESS)
            break;

        if ((result = tracker_queue_init( )) != SUCCESS)
            break;

        if ((result = snapshot_Init( )) != SUCCESS)
            break;

        if ((result = snapstore_device_init( )) != SUCCESS)
            break;
        if ((result = snapstore_init( )) != SUCCESS)
            break;

        if ((result = snapdata_collect_Init( )) != SUCCESS)
            break;

        if ((result = snapimage_init( )) != SUCCESS)
            break;

    }while(false);
/*

    conteiner_cnt = container_alloc_counter( );
    log_tr_d( "container_alloc_counter=", conteiner_cnt );

    conteiner_cnt = container_sl_alloc_counter( );
    log_tr_d( "container_sl_alloc_counter=", conteiner_cnt );
*/

    return result;
}

void __exit veeamsnap_exit(void)
{
    int conteiner_cnt = 0;
    int result;
    log_tr("Unloading module");

/*
    conteiner_cnt = container_alloc_counter( );
    log_tr_d( "start. container_alloc_counter=", conteiner_cnt );
    conteiner_cnt = container_sl_alloc_counter( );
    log_tr_d( "start. container_sl_alloc_counter=", conteiner_cnt );*/


#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
    //log_tr_d( "mem_cnt=", atomic_read( &g_mem_cnt ) );
    //log_tr_d( "vmem_cnt=", atomic_read( &g_vmem_cnt ) );
#endif


    result = snapshot_Done( );
    if (SUCCESS == result){

        snapdata_collect_Done( );

        snapstore_device_done( );
        snapstore_done( );

        result = tracker_done( );
        if (SUCCESS == result){
            result = tracker_queue_done( );
        }

        snapimage_done( );

        sparsebitmap_done( );



        blk_deferred_bioset_free( );
        blk_deferred_done( );

        blk_redirect_bioset_free( );
        blk_direct_bioset_free( );
    }

    if (SUCCESS != result){
        log_tr_d( "Failed to unload. errno=", result );
        return;
    }

    unregister_chrdev(veeamsnap_major, MODULE_NAME);

    ctrl_done( );

    logging_done( );

    conteiner_cnt = container_alloc_counter( );
    if (conteiner_cnt != 0)
        log_err_d( "container_alloc_counter=", conteiner_cnt );

    conteiner_cnt = container_sl_alloc_counter( );
    if (conteiner_cnt != 0)
        log_err_d( "container_sl_alloc_counter=", conteiner_cnt );

#ifdef VEEAMSNAP_MEMORY_LEAK_CONTROL
    if (atomic_read( &g_mem_cnt ) != 0)
        log_err_d( "mem_cnt=", atomic_read( &g_mem_cnt ) );
#endif

}

module_init(veeamsnap_init);
module_exit(veeamsnap_exit);


module_param_named( zerosnapdata, g_param_zerosnapdata, int, 0644 );
MODULE_PARM_DESC( zerosnapdata, "Zeroing snapshot data algorithm determine." );

module_param_named( debuglogging, g_param_debuglogging, int, 0644 );
MODULE_PARM_DESC( debuglogging, "Logging level switch." );

module_param( logdir, charp, 0644 );
MODULE_PARM_DESC( logdir, "Directory for module logs." );

module_param_named(snapstore_block_size_pow, g_param_snapstore_block_size_pow, int, 0644);
MODULE_PARM_DESC(snapstore_block_size_pow, "Snapstore block size binary pow. 20 for 1MiB block size");

module_param_named(change_tracking_block_size_pow, g_param_change_tracking_block_size_pow, int, 0644);
MODULE_PARM_DESC(change_tracking_block_size_pow, "Change-tracking block size binary pow. 18 for 256 KiB block size");

module_param_named(fixflags, g_param_fixflags, uint, 0644);
MODULE_PARM_DESC(fixflags, "Flags for known issues");

MODULE_LICENSE( LICENCE_STR );
MODULE_AUTHOR( AUTHOR_STR );

MODULE_DESCRIPTION( DESCRIPTION_STR );
MODULE_VERSION(FILEVER_STR);

MODULE_INFO( supported, "external" );
