#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#include "stdafx.h"
#include "shared_resource.h"
#include "cbt_map.h"
#include "log.h"

//////////////////////////////////////////////////////////////////////////
static inline void __cbt_map_init_lock( cbt_map_t* cbt_map )
{
#ifdef CBT_MAP_MUTEX_LOCK
	mutex_init( &cbt_map->locker );
#else
	spin_lock_init( &cbt_map->locker );
#endif
}
//////////////////////////////////////////////////////////////////////////
static inline void __cbt_map_lock( cbt_map_t* cbt_map )
{
#ifdef CBT_MAP_MUTEX_LOCK
	mutex_lock( &cbt_map->locker );
#else
	spin_lock( &cbt_map->locker );
#endif
}
//////////////////////////////////////////////////////////////////////////
static inline void __cbt_map_unlock( cbt_map_t* cbt_map )
{
#ifdef CBT_MAP_MUTEX_LOCK
	mutex_unlock( &cbt_map->locker );
#else
	spin_unlock( &cbt_map->locker );
#endif
}
//////////////////////////////////////////////////////////////////////////
#ifdef CBT_MAP_PAGES
static inline page_array_t* __get_writable( cbt_map_t* cbt_map )
{
	return cbt_map->write_map;
}

static inline page_array_t* __get_readable( cbt_map_t* cbt_map )
{
	return cbt_map->read_map;
}
#else
static inline byte_t* __get_writable( cbt_map_t* cbt_map )
{
	return cbt_map->write_map;
}

static inline byte_t* __get_readable( cbt_map_t* cbt_map )
{
	return cbt_map->read_map;
}
#endif
//////////////////////////////////////////////////////////////////////////
void cbt_map_destroy( cbt_map_t* cbt_map );

void cbt_map_destroy_cb( void* this_resource )
{
	cbt_map_t* cbt_map = (cbt_map_t*)this_resource;
	cbt_map_destroy( cbt_map );
}
//////////////////////////////////////////////////////////////////////////
cbt_map_t* cbt_map_create( unsigned int cbt_sect_in_block_degree, sector_t blk_dev_sect_count )
{
	int result = SUCCESS;
	cbt_map_t* cbt_map = NULL;

	log_traceln( "CBT map create." );

	cbt_map = (cbt_map_t*)dbg_kzalloc( sizeof( cbt_map_t ), GFP_KERNEL );
	if (cbt_map == NULL)
		return NULL;

	do{
		sector_t size_mod;
		cbt_map->sect_in_block_degree = cbt_sect_in_block_degree;

		cbt_map->map_size =( blk_dev_sect_count >> (sector_t)cbt_sect_in_block_degree );

		size_mod = (blk_dev_sect_count & ((sector_t)(1 << cbt_sect_in_block_degree) - 1));
		if (size_mod)
			cbt_map->map_size++;

#ifdef CBT_MAP_PAGES
		{
			size_t page_cnt = cbt_map->map_size >> PAGE_SHIFT;
			if (cbt_map->map_size & (PAGE_SIZE - 1))
				++page_cnt;

			cbt_map->read_map = page_array_alloc( page_cnt, GFP_KERNEL );
			if (cbt_map->read_map != NULL)
				page_array_memset( cbt_map->read_map, 0 );

			cbt_map->write_map = page_array_alloc( page_cnt, GFP_KERNEL );
			if (cbt_map->write_map != NULL)
				page_array_memset( cbt_map->write_map, 0 );
		}
#else
		cbt_map->read_map = dbg_vzalloc( cbt_map->map_size );
		cbt_map->write_map = dbg_vzalloc( cbt_map->map_size );
#endif
		if ((cbt_map->read_map == NULL) || (cbt_map->write_map == NULL)){
			log_errorln_sz( "Cannot allocate CBT map. map_size=", cbt_map->map_size );
			result = -ENOMEM;
		}
		__cbt_map_init_lock( cbt_map );

		init_rwsem( &cbt_map->rw_lock );
	} while (false);

	if (result == SUCCESS){
		cbt_map->snap_number_previous = 0;
		cbt_map->snap_number_active = 1;
        get_random_bytes( &cbt_map->generationId, 16 );

		shared_resource_init( &cbt_map->sharing_header, cbt_map, cbt_map_destroy_cb );
		return cbt_map;
	}

	cbt_map_destroy( cbt_map );
	return NULL;
}

void cbt_map_destroy( cbt_map_t* cbt_map )
{
	log_traceln( "CBT map destroy." );
	if (cbt_map != NULL){
		if (cbt_map->read_map != NULL){
#ifdef CBT_MAP_PAGES
			page_array_free( cbt_map->read_map );
#else
			dbg_vfree( cbt_map->read_map, cbt_map->map_size );
#endif
			cbt_map->read_map = NULL;
		}

		if (cbt_map->write_map != NULL){
#ifdef CBT_MAP_PAGES
			page_array_free( cbt_map->write_map );
#else
			dbg_vfree( cbt_map->write_map, cbt_map->map_size );
#endif
			cbt_map->write_map = NULL;
		}

		dbg_kfree( cbt_map );
	}
}

void cbt_map_switch( cbt_map_t* cbt_map )
{
	log_traceln( "CBT map switch." );
	__cbt_map_lock( cbt_map );
#ifdef CBT_MAP_PAGES
	page_array_memcpy( __get_readable( cbt_map ), __get_writable( cbt_map ) );
#else
	memcpy( __get_readable( cbt_map ), __get_writable( cbt_map ), cbt_map->map_size );
#endif
	cbt_map->snap_number_previous = cbt_map->snap_number_active;
	++cbt_map->snap_number_active;
	if (256 == cbt_map->snap_number_active){

		cbt_map->snap_number_active = 1;
#ifdef CBT_MAP_PAGES
		page_array_memset( __get_writable( cbt_map ), 0 );
#else
		memset( __get_writable( cbt_map ), 0, cbt_map->map_size );
#endif
        get_random_bytes( &cbt_map->generationId, 16 );

		log_traceln("Change tracking was reseted." );
	}
	__cbt_map_unlock( cbt_map );
}
#ifdef CBT_MAP_PAGES
int __cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt, byte_t snap_number, page_array_t* map )
#else
int __cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt, byte_t snap_number, byte_t* map )
#endif
{
#if 1
	size_t cbt_block;
	size_t cbt_block_first = ( size_t )(sector_start >> cbt_map->sect_in_block_degree);
	size_t cbt_block_last = (size_t)((sector_start + sector_cnt -1) >> cbt_map->sect_in_block_degree); //inclusive

	for (cbt_block = cbt_block_first; cbt_block <= cbt_block_last; ++cbt_block){
		if (cbt_block > cbt_map->map_size){
			log_errorln_sz( "Too much sectors reading. cbt_block=", cbt_block );
			return -EINVAL;
		}
#ifdef CBT_MAP_PAGES
		if (page_array_byte_get( map, cbt_block ) < snap_number){
			page_array_byte_set( map, cbt_block, snap_number );
		}
#else
		if (map[cbt_block] < snap_number){
			map[cbt_block] = snap_number;
		}
#endif
	}
	return SUCCESS;
#else
	sector_t sect_inx = sector_start;

	while (sect_inx < (sector_start + sector_cnt)){

		size_t cbt_block = (size_t)(sect_inx >> cbt_map->sect_in_block_degree);

		if (cbt_block > cbt_map->map_size){
			log_errorln_sz( "Too much sectors reading. cbt_block=", cbt_block );
			return -EINVAL;
		}
#ifdef CBT_MAP_PAGES
		if (page_array_byte_get( map, cbt_block ) < snap_number){
			page_array_byte_set( map, cbt_block, snap_number );
		}
#else
		if ( map[cbt_block] < snap_number){
			map[cbt_block] = snap_number;
		}
#endif

		sect_inx = (cbt_block << cbt_map->sect_in_block_degree) + (1<<cbt_map->sect_in_block_degree);
	}
	return SUCCESS;
#endif
}

int cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt )
{
	int res = SUCCESS;
	__cbt_map_lock( cbt_map );
	{
		byte_t snap_number = (byte_t)cbt_map->snap_number_active;

		res = __cbt_map_set( cbt_map, sector_start, sector_cnt, snap_number, __get_writable( cbt_map ) );
	}
	__cbt_map_unlock( cbt_map );
	return res;
}

int cbt_map_set_previous_both( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt )
{
	int res = SUCCESS;
	__cbt_map_lock( cbt_map );
	{
		byte_t snap_number = (byte_t)cbt_map->snap_number_previous;

		res = __cbt_map_set( cbt_map, sector_start, sector_cnt, snap_number, __get_writable( cbt_map ) );
		if (res == SUCCESS)
			res = __cbt_map_set( cbt_map, sector_start, sector_cnt, snap_number, __get_readable( cbt_map ) );
	}
	__cbt_map_unlock( cbt_map );
	return res;
}

size_t cbt_map_read_to_user( cbt_map_t* cbt_map, void* user_buff, size_t offset, size_t size )
{
	size_t readed = 0;
	do{
		size_t real_size = min( (cbt_map->map_size - offset), size );

		size_t left_size;
#ifdef CBT_MAP_PAGES
		{
			page_array_t* map = __get_readable( cbt_map );
			left_size = real_size - page_array_copy2user( user_buff, offset, map, real_size );
		}
#else
		{
			byte_t* map = __get_readable( cbt_map );
			left_size = copy_to_user( user_buff, &map[offset], real_size );
		}
#endif
		if (left_size == 0)
			readed = real_size;
		else{
			log_errorln_sz( "left size=", left_size );
			readed = real_size - left_size;
		}
	} while (false);

	return readed;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
