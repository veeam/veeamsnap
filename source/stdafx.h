#ifndef STDAFX_H_
#define STDAFX_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h>
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/bitmap.h>
#include <asm/atomic.h>
#include <linux/random.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
#define HAVE_MAKE_REQUEST_INT
#endif

#ifndef pr_warn
#define pr_warn pr_warning
#endif

#include "log.h"

#define LICENCE_STR "GPL"
#define AUTHOR_STR  "Veeam Software AG"


#define VEEAMSNAP_MEMORY_LEAK_CONTROL
#include "mem_alloc.h"

#define DESCRIPTION_STR "Veeam Snapshot Kernel Module"

#define MODULE_NAME "veeamsnap"
#define VEEAM_SNAP_IMAGE "veeamimage"

#define SECTOR512	512
#define SECTOR512_SHIFT 9
#define SECTORS_IN_PAGE (PAGE_SIZE / SECTOR512)

#define SUCCESS 0

#define SNAPSHOTDATA_MEMORY_SIZE ( 128 << 20 )

typedef unsigned char	byte_t;
typedef unsigned long long stream_size_t;

#define SNAPDATA_SPARSE_CHANGES // use sparse bitmap for snapdata collection
#define SPARSE_BLOCK_CACHEABLE  // use cache for sparse block arrays

#define DEFER_IO_COPY_REQUEST_LENGTH 10
#define DEFER_IO_DIO_REQUEST_LENGTH 250
#define DEFER_IO_DIO_REQUEST_SECTORS_COUNT (10*1024*1024/SECTOR512)

//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 30*HZ )    //delay 30 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 3*HZ )    //delay 3 sec
#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 1*HZ )    //delay 1 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( HZ/1000 * 10 )    //delay 10 ms

int get_debuglogging( void );
#define VEEAM_LL_DEFAULT   0	/* default as normal*/
#define VEEAM_LL_LO	       2	/* minimal logging */
#define VEEAM_LL_NORM	   4	/* normal */
#define VEEAM_LL_HI  	   7	/* debug logging */

#define VEEAM_ZEROSNAPDATA_OFF 0
#define VEEAM_ZEROSNAPDATA_ON  1
int get_zerosnapdata( void );

#define SNAPDATA_ZEROED

#define CBT_BLOCK_SIZE_DEGREE ( 9 + SECTOR512_SHIFT ) //256Kb
#define CBT_BLOCK_SIZE (1<<CBT_BLOCK_SIZE_DEGREE)

#define SNAPSTORE

#ifdef SNAPSTORE

#define COW_BLOCK_SIZE_DEGREE ( 11 + SECTOR512_SHIFT ) //1MiB
#define COW_BLOCK_SIZE (1<<COW_BLOCK_SIZE_DEGREE)

#define SNAPSTORE_BLK_SHIFT (sector_t)(COW_BLOCK_SIZE_DEGREE - SECTOR512_SHIFT)
#define SNAPSTORE_BLK_SIZE  (sector_t)(1 << SNAPSTORE_BLK_SHIFT)
#define SNAPSTORE_BLK_MASK  (sector_t)(SNAPSTORE_BLK_SIZE-1)

#endif

//#define VEEAM_IOCTL_LOGGING


#endif /* STDAFX_H_ */
