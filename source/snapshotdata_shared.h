#pragma once

#ifndef SNAPSTORE

#include "container.h"
#include "shared_resource.h"
#include "uuid_util.h"

#define SNAPSHOTDATA_TYPE_INVALID 0
#define SNAPSHOTDATA_TYPE_MEM  1
#define SNAPSHOTDATA_TYPE_COMMON  2
#define SNAPSHOTDATA_TYPE_STRETCH 3

typedef struct snapshotdata_shared_s{
	content_t content;
	shared_resource_t sharing_header;
	veeam_uuid_t unique_id;
	char type;
}snapshotdata_shared_t;


static inline void snapshotdata_shared_create( snapshotdata_shared_t* shared, veeam_uuid_t* id, char type, shared_resource_free_cb* free_cb )
{
	veeam_uuid_copy( &shared->unique_id, id );

	shared_resource_init( &shared->sharing_header, shared, free_cb );
	shared->type = type;
}

static inline snapshotdata_shared_t* snapshotdata_shared_get_resource( snapshotdata_shared_t* resource )
{
	return (snapshotdata_shared_t*)shared_resource_get( &resource->sharing_header );
}

static inline void snapshotdata_shared_put_resource( snapshotdata_shared_t* resource )
{
	shared_resource_put( &resource->sharing_header );
}

static inline int snapshotdata_shared_own_cnt( snapshotdata_shared_t* shared )
{
	return atomic_read( &shared->sharing_header.own_cnt );
}

snapshotdata_shared_t* snapshotdata_shared_find( veeam_uuid_t* id, container_t* Snapshot );

static inline bool snapshotdata_shared_id_equal( veeam_uuid_t* id, snapshotdata_shared_t* shared )
{
	return veeam_uuid_equal( id, &shared->unique_id );
}

snapshotdata_shared_t* snapshotdata_shared_find_by_id( veeam_uuid_t* id );

#endif //SNAPSTORE

