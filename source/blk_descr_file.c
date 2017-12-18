#include "stdafx.h"
#include "blk_descr_file.h"

//#define _TRACE

void blk_descr_file_init( blk_descr_file_t* blk_descr, rangelist_t* rangelistr )
{
	blk_descr_unify_init( &blk_descr->unify );

	rangelist_copy( &blk_descr->rangelist, rangelistr );
}

void blk_descr_file_done( blk_descr_file_t* blk_descr )
{
	rangelist_done( &blk_descr->rangelist );
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

	blk_descr_file_t blocks[0];
}_pool_el_t;

static _pool_el_t* _pool_el_alloc( void )
{
	size_t el_size;
	_pool_el_t* el = (_pool_el_t*)dbg_kmalloc_huge( _POOL_EL_MAX_SIZE, PAGE_SIZE, GFP_NOIO, &el_size );
	if (NULL == el)
		return NULL;

#ifdef _TRACE
	log_warnln( "++" );
#endif

	el->capacity = (el_size - sizeof( _pool_el_t )) / sizeof( blk_descr_file_t );

	//log_traceln_sz( "allocated block size=", el_size );
	//log_traceln_sz( "block capacity =", el->capacity );

	el->used_cnt = 0;

	INIT_LIST_HEAD( &el->link );

	return el;
}

static void _pool_el_free( _pool_el_t* el )
{
	if (el != NULL){
		dbg_kfree( el );
#ifdef _TRACE
		log_warnln( "--" );
#endif
	}
}


void blk_descr_file_pool_init( blk_descr_file_pool_t* pool )
{
	init_rwsem( &pool->lock );

	INIT_LIST_HEAD( &pool->head );

	pool->blocks_cnt = 0;

	pool->total_cnt = 0;
	pool->take_cnt = 0;
}

void _blk_descr_file_pool_cleanup( blk_descr_file_pool_t* pool )
{
	down_write( &pool->lock );
	while (!list_empty( &pool->head )){
		size_t inx;
		_pool_el_t* el = list_entry( pool->head.next, _pool_el_t, link );
		if (el == NULL)
			break;

		list_del( &el->link );
		--pool->blocks_cnt;

		pool->total_cnt -= el->used_cnt;

		for (inx = 0; inx < el->used_cnt; ++inx)
			blk_descr_file_done( &el->blocks[inx] );

		_pool_el_free( el );

	}
	up_write( &pool->lock );
}

void blk_descr_file_pool_done( blk_descr_file_pool_t* pool )
{
	_blk_descr_file_pool_cleanup( pool );
}


blk_descr_file_t* _blk_descr_file_pool_alloc( blk_descr_file_pool_t* pool, rangelist_t* rangelist )
{
	blk_descr_file_t* blk_descr = NULL;

	//log_warnln( "<" );

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
		blk_descr_file_init( blk_descr, rangelist );

		++el->used_cnt;
		++pool->total_cnt;

	} while (false);
	up_write( &pool->lock );

	//log_warnln( ">" );
	return blk_descr;
}

int blk_descr_file_pool_add( blk_descr_file_pool_t* pool, rangelist_t* rangelist )
{
	blk_descr_file_t* blk_descr;

	//log_warnln( "<" );

	blk_descr = _blk_descr_file_pool_alloc( pool, rangelist );
	if (blk_descr == NULL){
		log_errorln( "Failed to allocate block descriptor" );
		return -ENOMEM;
	}

	//log_warnln( ">" );
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

blk_descr_file_t* blk_descr_file_pool_at( blk_descr_file_pool_t* pool, size_t index )
{
	blk_descr_file_t* bkl_descr = NULL;
	size_t curr_inx = 0;
	_pool_el_t* el;

	down_read( &pool->lock );
	_FOREACH_EL_BEGIN( pool, el )
	{

		if ((index >= curr_inx) && (index < (curr_inx + el->used_cnt))){
			bkl_descr = el->blocks + (index - curr_inx);

			//log_warnln_sz( "Found by index", index );
			break;
		}
		curr_inx += el->used_cnt;
	}
	_FOREACH_EL_END( );
	up_read( &pool->lock );

	return bkl_descr;
}

blk_descr_file_t* blk_descr_file_pool_take( blk_descr_file_pool_t* pool )
{
	blk_descr_file_t* result = NULL;

	if (pool->take_cnt >= pool->total_cnt){
		log_errorln_sz( "take_cnt=", pool->take_cnt );
		log_errorln_sz( "total_cnt=", pool->total_cnt );
		return NULL;
	}

	result = blk_descr_file_pool_at( pool, pool->take_cnt );
	if (result == NULL){
		log_errorln_sz( "total_cnt=", pool->total_cnt );
		return NULL;
	}

	++pool->take_cnt;

	return result;
}

