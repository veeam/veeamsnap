#include "stdafx.h"
#include "container_spinlocking.h"
#include "tracker_queue.h"

#if  LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)

#ifdef HAVE_MAKE_REQUEST_INT
int tracking_make_request(struct request_queue *q, struct bio *bio);
#else
void tracking_make_request(struct request_queue *q, struct bio *bio);
#endif

#else
blk_qc_t tracking_make_request( struct request_queue *q, struct bio *bio );
#endif


container_sl_t TrackerQueueList;

int tracker_queue_Init(void )
{
	container_sl_init(&TrackerQueueList, sizeof(tracker_queue_t));
	return SUCCESS;
}

int tracker_queue_Done(void )
{
	int result = container_sl_done( &TrackerQueueList );
	if (SUCCESS != result){
		log_errorln_s( "Failed to free container =", "vsnap_TrackerQueueList" );
	}
	return result;
}

// find or create new tracker queue
int tracker_queue_Ref(
	struct request_queue* queue,
	tracker_queue_t** ppTrackerQueue
)
{
	int find_result = SUCCESS;
	tracker_queue_t* pTrQ = NULL;

	find_result = tracker_queue_Find(queue, &pTrQ);
	if (SUCCESS == find_result){
		log_traceln_p("Queue already exist! queue=0x", queue);

		*ppTrackerQueue = pTrQ;
		atomic_inc( &pTrQ->atomRefCount );

		return find_result;
	}

	if (-ENODATA != find_result){
		log_traceln_p("Failed to find queue=0x", queue);
		return find_result;
	}
	log_traceln_p("Creating tracker_queue for queue=", queue );
	//if ENODATA then create new object for queue
	pTrQ = (tracker_queue_t*)container_sl_new(&TrackerQueueList);
	if (NULL==pTrQ)
		return -ENOMEM;

	atomic_set( &pTrQ->atomRefCount, 0 );

	pTrQ->TargetMakeRequest_fn = queue->make_request_fn;
	queue->make_request_fn = tracking_make_request;

	pTrQ->pTargetQueue = queue;

	*ppTrackerQueue = pTrQ;
	atomic_inc( &pTrQ->atomRefCount );

	log_traceln_p("Tracker queue created. pTrQ=",pTrQ);

	return SUCCESS;
}

void tracker_queue_Unref(
	tracker_queue_t* pTrackerQueue
)
{
	if ( atomic_dec_and_test( &pTrackerQueue->atomRefCount ) ){

		if (NULL != pTrackerQueue->TargetMakeRequest_fn){
			pTrackerQueue->pTargetQueue->make_request_fn = pTrackerQueue->TargetMakeRequest_fn;
			pTrackerQueue->TargetMakeRequest_fn = NULL;
		}

		container_sl_free( &pTrackerQueue->content );

		log_traceln_p("Tracker queue freed. pTrQ=",pTrackerQueue);
	}else{
		log_traceln_p("Tracker queue still live. pTrQ=",pTrackerQueue);
	}
}



int tracker_queue_Find( struct request_queue* queue, tracker_queue_t** ppTrackerQueue )
{
	int result = -ENODATA;
	content_sl_t* pContent = NULL;
	tracker_queue_t* pTrQ = NULL;
	CONTAINER_SL_FOREACH_BEGIN( TrackerQueueList, pContent )
	{
		pTrQ = (tracker_queue_t*)pContent;
		if (pTrQ->pTargetQueue == queue){
			*ppTrackerQueue = pTrQ;

			result = SUCCESS;	//don`t continue
			break;
		}
	}CONTAINER_SL_FOREACH_END( TrackerQueueList );

	return result;
}

