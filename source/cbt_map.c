#include "stdafx.h"
#include "cbt_map.h"


static inline void _cbt_map_init_lock( cbt_map_t* cbt_map )
{
	spin_lock_init( &cbt_map->locker );
}


static inline void _cbt_map_lock( cbt_map_t* cbt_map )
{
	spin_lock( &cbt_map->locker );
}


static inline void _cbt_map_unlock( cbt_map_t* cbt_map )
{
	spin_unlock( &cbt_map->locker );
}


static inline page_array_t* _get_writable( cbt_map_t* cbt_map )
{
	return cbt_map->write_map;
}

static inline page_array_t* _get_readable( cbt_map_t* cbt_map )
{
	return cbt_map->read_map;
}


void cbt_map_destroy( cbt_map_t* cbt_map );

void cbt_map_destroy_cb( void* this_resource )
{
	cbt_map_t* cbt_map = (cbt_map_t*)this_resource;
	cbt_map_destroy( cbt_map );
}

int cbt_map_allocate( cbt_map_t* cbt_map, unsigned int cbt_sect_in_block_degree, sector_t blk_dev_sect_count )
{
	sector_t size_mod;
	cbt_map->sect_in_block_degree = cbt_sect_in_block_degree;

	cbt_map->map_size = (blk_dev_sect_count >> (sector_t)cbt_sect_in_block_degree);

	size_mod = (blk_dev_sect_count & ((sector_t)(1 << cbt_sect_in_block_degree) - 1));
	if (size_mod)
		cbt_map->map_size++;

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

	if ((cbt_map->read_map == NULL) || (cbt_map->write_map == NULL)){
		log_errorln_sz( "Cannot allocate CBT map. map_size=", cbt_map->map_size );
		return -ENOMEM;
	}

	cbt_map->snap_number_previous = 0;
	cbt_map->snap_number_active = 1;
	uuid_gen( cbt_map->generationId.b );
	cbt_map->active = true;
	
	return SUCCESS;
}

void cbt_map_deallocate( cbt_map_t* cbt_map )
{
	if (cbt_map->read_map != NULL){
		page_array_free( cbt_map->read_map );
		cbt_map->read_map = NULL;
	}

	if (cbt_map->write_map != NULL){
		page_array_free( cbt_map->write_map );
		cbt_map->write_map = NULL;
	}

	cbt_map->active = false;
}

cbt_map_t* cbt_map_create( unsigned int cbt_sect_in_block_degree, sector_t blk_dev_sect_count )
{
	cbt_map_t* cbt_map = NULL;

	log_traceln( "CBT map create." );

	cbt_map = (cbt_map_t*)dbg_kzalloc( sizeof( cbt_map_t ), GFP_KERNEL );
	if (cbt_map == NULL)
		return NULL;

	if (SUCCESS == cbt_map_allocate( cbt_map, cbt_sect_in_block_degree, blk_dev_sect_count )){
		_cbt_map_init_lock( cbt_map );

		init_rwsem( &cbt_map->rw_lock );

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
		cbt_map_deallocate( cbt_map );

		dbg_kfree( cbt_map );
	}
}

void cbt_map_switch( cbt_map_t* cbt_map )
{
	log_traceln( "CBT map switch." );
	_cbt_map_lock( cbt_map );

	page_array_memcpy( _get_readable( cbt_map ), _get_writable( cbt_map ) );

	cbt_map->snap_number_previous = cbt_map->snap_number_active;
	++cbt_map->snap_number_active;
	if (256 == cbt_map->snap_number_active){

		cbt_map->snap_number_active = 1;

		page_array_memset( _get_writable( cbt_map ), 0 );

		uuid_gen( cbt_map->generationId.b );

		log_traceln("Change tracking was reseted." );
	}
	_cbt_map_unlock( cbt_map );
}

int _cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt, byte_t snap_number, page_array_t* map )
{
	int res = SUCCESS;
	size_t cbt_block;
	size_t cbt_block_first = ( size_t )(sector_start >> cbt_map->sect_in_block_degree);
	size_t cbt_block_last = (size_t)((sector_start + sector_cnt -1) >> cbt_map->sect_in_block_degree); //inclusive

	for (cbt_block = cbt_block_first; cbt_block <= cbt_block_last; ++cbt_block){
		if (cbt_block < cbt_map->map_size){
			byte_t num;
			res = page_array_byte_get( map, cbt_block, &num );
			if (SUCCESS == res){
				if (num < snap_number){
					res = page_array_byte_set( map, cbt_block, snap_number );
				}
			}
		}
		else
			res = -EINVAL;

		if (SUCCESS != res){
			log_errorln_sz( "Too much sectors reading. cbt_block=", cbt_block );
			log_errorln_sz( "map_size=", cbt_map->map_size );
			break;
		}
	}
	return res;
}

int cbt_map_set( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt )
{
	int res = SUCCESS;
	_cbt_map_lock( cbt_map );
	{
		byte_t snap_number = (byte_t)cbt_map->snap_number_active;

		res = _cbt_map_set( cbt_map, sector_start, sector_cnt, snap_number, _get_writable( cbt_map ) );
	}
	_cbt_map_unlock( cbt_map );
	return res;
}

int cbt_map_set_previous_both( cbt_map_t* cbt_map, sector_t sector_start, sector_t sector_cnt )
{
	int res = SUCCESS;
	_cbt_map_lock( cbt_map );
	{
		byte_t snap_number = (byte_t)cbt_map->snap_number_previous;

		res = _cbt_map_set( cbt_map, sector_start, sector_cnt, snap_number, _get_writable( cbt_map ) );
		if (res == SUCCESS)
			res = _cbt_map_set( cbt_map, sector_start, sector_cnt, snap_number, _get_readable( cbt_map ) );
	}
	_cbt_map_unlock( cbt_map );
	return res;
}

size_t cbt_map_read_to_user( cbt_map_t* cbt_map, void* user_buff, size_t offset, size_t size )
{
	size_t readed = 0;
	do{
		size_t real_size = min( (cbt_map->map_size - offset), size );

		size_t left_size;

		{
			page_array_t* map = _get_readable( cbt_map );
			left_size = real_size - page_array_page2user( user_buff, offset, map, real_size );
		}

		if (left_size == 0)
			readed = real_size;
		else{
			log_errorln_sz( "left size=", left_size );
			readed = real_size - left_size;
		}
	} while (false);

	return readed;
}

