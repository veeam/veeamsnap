#pragma once

#ifdef SNAPSTORE
#include "container.h"
#include "blk_descr_mem.h"


typedef struct snapstore_mem_s{

	container_t blocks_list;
	size_t blocks_limit;
	size_t blocks_allocated;

	blk_descr_mem_pool_t pool;
}snapstore_mem_t;

snapstore_mem_t* snapstore_mem_create( unsigned long long buffer_size );

void snapstore_mem_destroy( snapstore_mem_t* mem );

void* snapstore_mem_get_block( snapstore_mem_t* mem );

bool snapstore_mem_check_halffill( snapstore_mem_t* mem, sector_t empty_limit, sector_t* fill_status );

#endif //SNAPSTORE
