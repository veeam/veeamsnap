#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#include "stdafx.h"
#include "container_spinlocking.h"


static atomic_t g_container_sl_alloc_cnt = ATOMIC_INIT( 0 );
//////////////////////////////////////////////////////////////////////////
int container_sl_alloc_counter( void )
{
	return atomic_read( &g_container_sl_alloc_cnt );
}
//////////////////////////////////////////////////////////////////////////
int container_sl_init( container_sl_t* pContainer, int content_size, char* cache_name )
{
	INIT_LIST_HEAD( &pContainer->headList );

	rwlock_init( &pContainer->lock );

	pContainer->content_size = content_size;

	atomic_set( &pContainer->cnt, 0 );

	if (cache_name != NULL){
		pContainer->content_cache = kmem_cache_create( cache_name, content_size, 0, 0, NULL );
		if (pContainer->content_cache == NULL){
			log_traceln_s( "Cannot create kmem_cache. Name=", cache_name );
			return -ENOMEM;
		}
	}
	else
		pContainer->content_cache = NULL;

	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int container_sl_done( container_sl_t* pContainer )
{
	int cnt;

	if (pContainer->content_size != 0){
		cnt = atomic_read( &pContainer->cnt );
		if ((cnt != 0) || !list_empty( &pContainer->headList )){
			log_errorln_d( "CRITICAL ERROR: Container isn`t empty! length=", cnt );
			return -EBUSY;
		}

		if (pContainer->content_cache != NULL){
			kmem_cache_destroy( pContainer->content_cache );
			pContainer->content_cache = NULL;
		}

		pContainer->content_size = 0;
	}
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
content_sl_t* container_sl_new( container_sl_t* pContainer )
{
	content_sl_t* pCnt = content_sl_new( pContainer );
	if (NULL != pCnt)
		container_sl_push_back( pContainer, pCnt );
	return pCnt;
}
//////////////////////////////////////////////////////////////////////////
void __container_sl_free( container_sl_t* pContainer, content_sl_t* pCnt )
{
	if (pCnt != NULL){
		write_lock( &pContainer->lock );
		{
			list_del( &pCnt->link );
		}
		write_unlock( &pContainer->lock );

		atomic_dec( &pContainer->cnt );

		content_sl_free( pCnt );
	}
}

void container_sl_free( content_sl_t* pCnt )
{
	container_sl_t* pContainer = pCnt->container;

	__container_sl_free(pContainer, pCnt);
}
//////////////////////////////////////////////////////////////////////////

int container_sl_length( container_sl_t* pContainer )
{
	return atomic_read( &pContainer->cnt );
}

bool container_sl_empty( container_sl_t* pContainer )
{
	return (0 == container_sl_length( pContainer ));
}

int container_sl_push_back( container_sl_t* pContainer, content_sl_t* pCnt )
{
	int index = 0;

	if (NULL != pCnt){
		INIT_LIST_HEAD( &pCnt->link );

		write_lock( &pContainer->lock );
		{
			list_add_tail( &pCnt->link, &pContainer->headList );
			index = atomic_inc_return( &pContainer->cnt );
		}
		write_unlock( &pContainer->lock );
	}

	return index;
}
void container_sl_get( content_sl_t* pCnt )
{
	container_sl_t* pContainer = pCnt->container;
	write_lock( &pContainer->lock );
	do{
		list_del( &pCnt->link );
		atomic_dec( &pContainer->cnt );
	} while (false);
	write_unlock( &pContainer->lock );
}

content_sl_t* container_sl_get_first( container_sl_t* pContainer )
{
	content_sl_t* pCnt = NULL;

	write_lock( &pContainer->lock );
	{
		if (!list_empty( &pContainer->headList )){
			pCnt = list_entry( pContainer->headList.next, content_sl_t, link );

			list_del( &pCnt->link );
			atomic_dec( &pContainer->cnt );
		}
	}
	write_unlock( &pContainer->lock );

	return pCnt;
}


content_sl_t* content_sl_new_opt( container_sl_t* pContainer, gfp_t gfp_opt )
{
	content_sl_t* pCnt;

	if (pContainer->content_cache != NULL)
		pCnt = kmem_cache_alloc( pContainer->content_cache, gfp_opt );
	else
		pCnt = dbg_kmalloc( pContainer->content_size, gfp_opt );

	if (pCnt){
		atomic_inc( &g_container_sl_alloc_cnt );
		memset( pCnt, 0, pContainer->content_size );

		pCnt->container = pContainer;
	}
	return pCnt;
}

content_sl_t* content_sl_new( container_sl_t* pContainer )
{
	return content_sl_new_opt( pContainer, GFP_KERNEL );
}

void content_sl_free( content_sl_t* pCnt )
{
	if (pCnt){
		container_sl_t* pContainer = pCnt->container;

		memset( pCnt, 0xFF, pContainer->content_size );

		atomic_dec( &g_container_sl_alloc_cnt );

		if (pContainer->content_cache != NULL)
			kmem_cache_free( pContainer->content_cache, pCnt );
		else
			dbg_kfree( pCnt );
	}
}
//////////////////////////////////////////////////////////////////////////
content_sl_t* container_sl_at( container_sl_t* pContainer, size_t inx )
{
	size_t count = 0;
	content_sl_t* pResult = NULL;
	content_sl_t* content = NULL;

	read_lock( &pContainer->lock );
	if (!list_empty( &pContainer->headList )){
		struct list_head* __container_list_head;
		list_for_each( __container_list_head, &pContainer->headList ){

			content = list_entry( __container_list_head, content_sl_t, link );
			if (inx == count){
				pResult = content;
				break;
			}
			++count;
	    }
    }
    read_unlock( &pContainer->lock );

	return pResult;
}
//////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
}

#endif  /* __cplusplus */
