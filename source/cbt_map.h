#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

//#define CBT_MAP_MUTEX_LOCK
#define CBT_MAP_PAGES

#ifdef CBT_MAP_PAGES
#include "page_array.h"
#endif

typedef struct cbt_map_s
{
	shared_resource_t sharing_header;
#ifdef CBT_MAP_MUTEX_LOCK
	struct mutex locker;
#else
	spinlock_t locker;
#endif

	size_t   sect_in_block_degree;
	size_t   map_size;
#ifdef CBT_MAP_PAGES
	page_array_t*  read_map;
	page_array_t*  write_map;
#else
	byte_t*  read_map;
	byte_t*  write_map;
#endif

	volatile unsigned long snap_number_active;
	volatile unsigned long snap_number_previous;
    unsigned char generationId[16];

	struct rw_semaphore rw_lock;
}cbt_map_t;

cbt_map_t* cbt_map_create( unsigned int cbt_sect_in_block_degree, sector_t blk_dev_sect_count );

void cbt_map_destroy( cbt_map_t* cbt_map );

void cbt_map_switch( cbt_map_t* cbt_map );

int cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt );

int cbt_map_set_previous_both( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt );

size_t cbt_map_read_to_user( cbt_map_t* cbt_map, void* user_buffer, size_t offset, size_t size );

//////////////////////////////////////////////////////////////////////////
static inline cbt_map_t* cbt_map_get_resource( cbt_map_t* cbt_map )
{
	if (cbt_map == NULL)
		return NULL;

	return (cbt_map_t*)shared_resource_get( &cbt_map->sharing_header );
}
//////////////////////////////////////////////////////////////////////////
static inline void cbt_map_put_resource( cbt_map_t* cbt_map )
{
	if (cbt_map != NULL)
		shared_resource_put( &cbt_map->sharing_header );
}
//////////////////////////////////////////////////////////////////////////
static inline void cbt_map_read_lock( cbt_map_t* cbt_map )
{
	down_read( &cbt_map->rw_lock );
};
static inline void cbt_map_read_unlock( cbt_map_t* cbt_map )
{
	up_read( &cbt_map->rw_lock );
};
static inline void cbt_map_write_lock( cbt_map_t* cbt_map )
{
	down_write( &cbt_map->rw_lock );
};
static inline void cbt_map_write_unlock( cbt_map_t* cbt_map )
{
	up_write( &cbt_map->rw_lock );
};
//////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
}
#endif  /* __cplusplus */
