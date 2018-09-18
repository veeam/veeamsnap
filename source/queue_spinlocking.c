#include "stdafx.h"
#include "queue_spinlocking.h"

int queue_sl_init( queue_sl_t* queue, int content_size )
{
	INIT_LIST_HEAD( &queue->headList );

	spin_lock_init( &queue->lock );

	queue->content_size = content_size;

	atomic_set( &queue->in_queue_cnt, 0);
	atomic_set( &queue->alloc_cnt, 0 );

	atomic_set( &queue->active_state, true );
	return SUCCESS;
}

int queue_sl_done( queue_sl_t* queue )
{
	if (queue->content_size != 0){
		atomic_set( &queue->active_state, false );

		if (!list_empty( &queue->headList )){
			log_errorln_d( "CRITICAL ERROR: Queue isn`t empty! in_queue_cnt=", atomic_read( &queue->in_queue_cnt ) );
			return -EBUSY;
		}
		if (atomic_read( &queue->alloc_cnt )){
			log_errorln_d( "CRITICAL ERROR: Some content isn`t free! alloc_cnt=", atomic_read( &queue->alloc_cnt ) );
			return -EBUSY;
		}

		queue->content_size = 0;
	}
	return SUCCESS;
}

queue_content_sl_t* queue_content_sl_new_opt( queue_sl_t* queue, gfp_t gfp_opt )
{
	queue_content_sl_t* content = dbg_kmalloc( queue->content_size, gfp_opt );

	if (content){
		atomic_inc( &queue->alloc_cnt );
		memset( content, 0, queue->content_size );

		content->queue = queue;
	}
	return content;

}

void queue_content_sl_free( queue_content_sl_t* content )
{
	if (content){
		queue_sl_t* queue = content->queue;

		memset( content, 0xFF, queue->content_size );
		atomic_dec( &queue->alloc_cnt );

		dbg_kfree( content );
	}
}

#define _queue_sl_push_back( queue, content ) \
{ \
	INIT_LIST_HEAD( &(content)->link ); \
    list_add_tail( &(content)->link, &(queue)->headList ); \
    atomic_inc( &(queue)->in_queue_cnt ); \
}

int queue_sl_push_back( queue_sl_t* queue, queue_content_sl_t* content )
{
	int res = SUCCESS;
	spin_lock( &queue->lock );
	{
		if (atomic_read( &queue->active_state )){
			_queue_sl_push_back( queue, content );
		}
		else
			res = -EACCES;
	}
	spin_unlock( &queue->lock );
	return res;
}

#define _queue_sl_get_first( queue, content ) \
{ \
    content = list_entry( (queue)->headList.next, queue_content_sl_t, link ); \
    list_del( &(content)->link ); \
	atomic_dec( &(queue)->in_queue_cnt ); \
}

queue_content_sl_t* queue_sl_get_first( queue_sl_t* queue )
{
	queue_content_sl_t* content = NULL;

	spin_lock( &queue->lock );
	{
		if (!list_empty( &queue->headList )){
			_queue_sl_get_first( queue, content );
		}
	}
	spin_unlock( &queue->lock );
	return content;
}

bool queue_sl_active( queue_sl_t* queue, bool state )
{
	bool prev_state;
	spin_lock( &queue->lock );
	{
		prev_state = atomic_read( &queue->active_state );
		atomic_set( &queue->active_state, state );
	}
	spin_unlock( &queue->lock );
	return prev_state;
}



