#ifndef TRACKER_H
#define TRACKER_H

typedef struct tracker_s
{
	content_sl_t content;
	dev_t original_dev_id;

	struct block_device*	pTargetDev;
	tracker_queue_t*		pTrackerQueue;

	cbt_map_t* cbt_map;

	atomic_t				Freezed;

	defer_io_t*	            defer_io;

	volatile bool                    underChangeTracking;  // true if change tracking ON
	volatile unsigned long long      snapshot_id;          // current snapshot for this device
}tracker_t;

int tracker_Init( void );
int tracker_Done( void );

int tracker_FindByQueueAndSector( tracker_queue_t* pQueue, sector_t sector, tracker_t** ppTracker );
int tracker_FindByDevId( dev_t dev_id, tracker_t** ppTracker );

int tracker_EnumDevId( int max_count, dev_t* p_dev_id, int* p_count );
int tracker_EnumCbtInfo( int max_count, struct cbt_info_s* p_cbt_info, int* p_count );

int tracker_Freeze( snapshot_t* p_snapshot );
int tracker_Unfreeze( snapshot_t* p_snapshot );

int tracker_Create( unsigned long long snapshot_id, dev_t dev_id, unsigned int cbt_block_size_degree, tracker_t** ppTracker );
int tracker_Remove( tracker_t* pTracker );
int tracker_RemoveAll( void );

void tracker_CbtBitmapSet( tracker_t* pTracker, sector_t sector, sector_t sector_cnt );

void tracker_CbtBitmapLock( tracker_t* pTracker );
void tracker_CbtBitmapUnlock( tracker_t* pTracker );

void tracker_print_state( void );

#endif	//TRACKER_H
