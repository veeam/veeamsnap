#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

//////////////////////////////////////////////////////////////////////////
void dbg_mem_track_on( void );
void dbg_mem_track_off( void );
//////////////////////////////////////////////////////////////////////////
// memory allocation verify

extern atomic_t mem_cnt;
extern atomic_t vmem_cnt;

void dbg_mem_init( void );

size_t dbg_vmem_get_max_usage( void );

void dbg_kfree( const void *ptr );
void *dbg_kzalloc( size_t size, gfp_t flags );
void *dbg_kmalloc( size_t size, gfp_t flags );
//////////////////////////////////////////////////////////////////////////

void *dbg_vmalloc( size_t size );
void *dbg_vzalloc( size_t size );
void dbg_vfree( const void *ptr, size_t size );

void * dbg_kmalloc_huge( size_t max_size, size_t min_size, gfp_t flags, size_t* p_allocated_size );
//////////////////////////////////////////////////////////////////////////
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
#define mem_kmap_atomic(__page) kmap_atomic(__page,KM_BOUNCE_READ)
#define mem_kunmap_atomic(__mem) kunmap_atomic(__mem,KM_BOUNCE_READ)
#else
#define mem_kmap_atomic(__page) kmap_atomic(__page)
#define mem_kunmap_atomic(__mem) kunmap_atomic(__mem)
#endif
//////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif  /* __cplusplus */
