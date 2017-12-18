#pragma once
#include "blk_descr_unify.h"

typedef struct blk_descr_mem_s
{
	blk_descr_unify_t unify;

	void* buff; //pointer to snapstore block in memory
}blk_descr_mem_t;



typedef struct blk_descr_mem_pool_s
{
	struct list_head head;
	struct rw_semaphore lock;

	size_t blocks_cnt; //count of _pool_el_t

	size_t total_cnt; ///count of blk_descr_mem_t
	size_t take_cnt; // take count of blk_descr_mem_t;
}blk_descr_mem_pool_t;

void blk_descr_mem_pool_init( blk_descr_mem_pool_t* pool );
void blk_descr_mem_pool_done( blk_descr_mem_pool_t* pool );

int blk_descr_mem_pool_add( blk_descr_mem_pool_t* pool, void* buffer );
blk_descr_mem_t* blk_descr_mem_pool_take( blk_descr_mem_pool_t* pool );

blk_descr_mem_t* blk_descr_mem_pool_add_and_take( blk_descr_mem_pool_t* pool, void* buffer );