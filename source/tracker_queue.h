#pragma once
#include "container_spinlocking.h"

typedef struct _tracker_queue_s
{
	content_sl_t content;

	struct request_queue*	pTargetQueue;
	make_request_fn*		TargetMakeRequest_fn;

	atomic_t				atomRefCount;

}tracker_queue_t;

int tracker_queue_Init(void );
int tracker_queue_Done(void );


int tracker_queue_Ref(
	struct request_queue* queue,
	tracker_queue_t** ppTrackerQueue
);

void tracker_queue_Unref(
	tracker_queue_t* pTrackerQueue
);

int tracker_queue_Find(
	struct request_queue* queue,
	tracker_queue_t** ppTrackerQueue
);


