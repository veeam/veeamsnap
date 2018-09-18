#include "stdafx.h"
#include "blk_descr_mem.h"


void blk_descr_mem_init( blk_descr_mem_t* blk_descr, void* ptr )
{
	blk_descr_unify_init( &blk_descr->unify );

	blk_descr->buff = ptr;
	//blk_descr->sect_cnt = sect_cnt;
}

//////////////////////////////////////////////////////////////////////////
// pool
//////////////////////////////////////////////////////////////////////////

#define _POOL_EL_MAX_SIZE (8*PAGE_SIZE)

typedef struct _pool_el_s
{
	struct list_head link;

	size_t used_cnt; // used blocks
	size_t capacity; // blocks array capacity

	blk_descr_mem_t blocks[0];
}_pool_el_t;

static _pool_el_t* _pool_el_alloc( void )
{
	size_t el_size = 0;
	_pool_el_t* el = (_pool_el_t*)dbg_kmalloc_huge( _POOL_EL_MAX_SIZE, PAGE_SIZE, GFP_KERNEL, &el_size );

	if (NULL == el)
		return NULL;

#ifdef _TRACE
	log_warnln( "++" );
#endif

	el->capacity = (el_size - sizeof( _pool_el_t )) / sizeof( blk_descr_mem_t );

	//log_errorln_sz( "allocated block size=", el_size );
	//log_errorln_sz( "block capacity =", el->capacity );

	el->used_cnt = 0;

	INIT_LIST_HEAD( &el->link );

	return el;
}

static void _pool_el_free( _pool_el_t* el )
{
	if (el != NULL){
#ifdef _TRACE
		log_warnln( "--" );
#endif
		dbg_kfree( el );
	}
}


void blk_descr_mem_pool_init( blk_descr_mem_pool_t* pool )
{
	init_rwsem( &pool->lock );

	INIT_LIST_HEAD( &pool->head );

	pool->blocks_cnt = 0;

	pool->total_cnt = 0;
	pool->take_cnt = 0;
}

void _blk_descr_mem_pool_cleanup( blk_descr_mem_pool_t* pool )
{
	down_write( &pool->lock );
	while (!list_empty( &pool->head )){

		_pool_el_t* el = list_entry( pool->head.next, _pool_el_t, link );
		if (el == NULL)
			break;

		list_del( &el->link );
		--pool->blocks_cnt;

		pool->total_cnt -= el->used_cnt;

		_pool_el_free( el );

	}
	up_write( &pool->lock );
}

void blk_descr_mem_pool_done( blk_descr_mem_pool_t* pool )
{
	_blk_descr_mem_pool_cleanup( pool );
}

blk_descr_mem_t* _blk_descr_mem_pool_alloc( blk_descr_mem_pool_t* pool, void* buffer )
{
	blk_descr_mem_t* blk_descr = NULL;

	down_write( &pool->lock );
	do{
		_pool_el_t* el = NULL;

		if (!list_empty( &pool->head )){
			el = list_entry( pool->head.prev, _pool_el_t, link );
			if (el->used_cnt == el->capacity)
				el = NULL;
		}

		if (el == NULL){
			el = _pool_el_alloc( );
			if (NULL == el)
				break;

			list_add_tail( &el->link, &pool->head );

			++pool->blocks_cnt;
		}

		blk_descr = &el->blocks[el->used_cnt];
		blk_descr_mem_init( blk_descr, buffer );

		++el->used_cnt;
		++pool->total_cnt;

	} while (false);
	up_write( &pool->lock );

	return blk_descr;
}

int blk_descr_mem_pool_add( blk_descr_mem_pool_t* pool, void* buffer )
{
	blk_descr_mem_t* blk_descr = _blk_descr_mem_pool_alloc( pool, buffer );
	if (blk_descr == NULL)
		return -ENOMEM;
	return SUCCESS;
}


#define _FOREACH_EL_BEGIN( pool, el )  \
if (!list_empty( &(pool)->head )){ \
    struct list_head* _list_head; \
	list_for_each( _list_head, &(pool)->head ){ \
        el = list_entry( _list_head, _pool_el_t, link );

#define _FOREACH_EL_END( ) \
	} \
}


blk_descr_mem_t* blk_descr_mem_pool_at( blk_descr_mem_pool_t* pool, size_t index )
{
	blk_descr_mem_t* bkl_descr = NULL;
	size_t curr_inx = 0;
	_pool_el_t* el;

	down_read( &pool->lock );
	_FOREACH_EL_BEGIN( pool, el )
	{
		if ((index >= curr_inx) && (index < (curr_inx + el->used_cnt))){
			bkl_descr = el->blocks + (index - curr_inx);
			break;
		}
		curr_inx += el->used_cnt;
	}
	_FOREACH_EL_END( );
	up_read( &pool->lock );

	return bkl_descr;
}


blk_descr_mem_t* blk_descr_mem_pool_take( blk_descr_mem_pool_t* pool )
{
	blk_descr_mem_t* result = NULL;
	if (pool->take_cnt < pool->total_cnt){
		result = blk_descr_mem_pool_at( pool, pool->take_cnt );
		++pool->take_cnt;
	}
	return result;
}

blk_descr_mem_t* blk_descr_mem_pool_add_and_take( blk_descr_mem_pool_t* pool, void* buffer )
{
	blk_descr_mem_t* blk_descr = _blk_descr_mem_pool_alloc( pool, buffer );
	if (blk_descr != NULL)
		++pool->take_cnt;
	return blk_descr;
}