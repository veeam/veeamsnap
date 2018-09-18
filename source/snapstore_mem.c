#include "stdafx.h"
#ifdef SNAPSTORE

#include "snapstore_mem.h"

typedef struct buffer_el_s{
	content_t content;
	void* buff;
}buffer_el_t;


snapstore_mem_t* snapstore_mem_create( unsigned long long buffer_size_limit )
{
	int res;
	snapstore_mem_t* mem = dbg_kzalloc( sizeof( snapstore_mem_t ), GFP_KERNEL );
	if (mem == NULL)
		return NULL;

	blk_descr_mem_pool_init( &mem->pool );

	mem->blocks_limit = (size_t)(buffer_size_limit >> (SNAPSTORE_BLK_SHIFT + SECTOR512_SHIFT));
	do{
		res = container_init( &mem->blocks_list, sizeof( buffer_el_t ) );
		if (res != SUCCESS)
			break;


	} while (false);
	if (res != SUCCESS){
		dbg_kfree( mem );
		mem = NULL;
	}

	return mem;
}

void snapstore_mem_destroy( snapstore_mem_t* mem )
{
	if (mem != NULL){
		buffer_el_t* buffer_el = NULL;

		while ( NULL != (buffer_el = (buffer_el_t*)container_get_first( &mem->blocks_list )) )
		{
			dbg_vfree( buffer_el->buff, SNAPSTORE_BLK_SIZE * SECTOR512 );
			content_free( &buffer_el->content );
		}

		if (SUCCESS != container_done( &mem->blocks_list )){
			log_errorln( "Container is not empty" );
		};

		blk_descr_mem_pool_done( &mem->pool );

		dbg_kfree( mem );
	}
}

void* snapstore_mem_get_block( snapstore_mem_t* mem )
{
	buffer_el_t* buffer_el;

	if (mem->blocks_allocated >= mem->blocks_limit){
		log_errorln("snapstore memory allocation block limit achieved" );
		return NULL;
	}

	buffer_el = (buffer_el_t*)content_new( &mem->blocks_list );
	if (buffer_el == NULL)
		return NULL;

	buffer_el->buff = dbg_vmalloc( SNAPSTORE_BLK_SIZE * SECTOR512 );
	if (buffer_el->buff == NULL){
		content_free( &buffer_el->content );
		return NULL;
	}

	++mem->blocks_allocated;
	if (0 == (mem->blocks_allocated & 0x7F)){
		log_traceln_sz( "filled blocks ", mem->blocks_allocated );
	}

	container_push_back( &mem->blocks_list, &buffer_el->content );
	return buffer_el->buff;
}

bool snapstore_mem_check_halffill( snapstore_mem_t* mem, sector_t empty_limit, sector_t* fill_status )
{
	size_t empty_blocks = (mem->pool.total_cnt - mem->pool.take_cnt);

	*fill_status = (sector_t)(mem->pool.take_cnt) << SNAPSTORE_BLK_SHIFT;

	return (empty_blocks < (size_t)(empty_limit >> SNAPSTORE_BLK_SHIFT));

}

#define BUFFER_OVERFLOW_CHECK(_blk_ofs,_blk_cnt,_buff_size ) \
if ((sector_to_size( _blk_ofs ) + sector_to_size( _blk_cnt )) > _buff_size){ \
    log_errorln( "Buffer overflow" ); \
    log_errorln_sz( "buffer size = ", _buff_size ); \
    log_errorln_sz( "offset = ", sector_to_size( _blk_ofs ) ); \
    log_errorln_sz( "length = ", sector_to_size( _blk_cnt ) ); \
    return -EINVAL; \
}

#endif //SNAPSTORE
