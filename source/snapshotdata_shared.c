#include "stdafx.h"
#include "snapshotdata_shared.h"

#ifndef SNAPSTORE

#include "snapshotdata_stretch.h"
#include "snapshotdata_common.h"
#include "snapshotdata_memory.h"


snapshotdata_shared_t* snapshotdata_shared_find( veeam_uuid_t* id, container_t* Snapshot )
{
	snapshotdata_shared_t* result = NULL;
	content_t* content = NULL;

	CONTAINER_FOREACH_BEGIN( (*Snapshot), content )
	{
		snapshotdata_shared_t* shared = (snapshotdata_shared_t*)content;

		if (snapshotdata_shared_id_equal( id, shared )){
			result = shared;
			break;
		}
	}
	CONTAINER_FOREACH_END( (*Snapshot) )

	return result;
}

snapshotdata_shared_t* snapshotdata_shared_find_by_id( veeam_uuid_t* id )
{
	{
		snapshotdata_stretch_t* stretch = snapshotdata_stretch_find( id );
		if (stretch != NULL)
			return &stretch->shared;
	}
	{
		snapshotdata_common_t* comm_disk = snapshotdata_common_find( id );
		if (comm_disk != NULL)
			return &comm_disk->shared;
	}
	{
		snapshotdata_memory_t* mem = snapshotdata_memory_find( id );
		if (mem != NULL)
			return &mem->shared;
	}

	log_errorln_uuid( "Cannot find common file ", id );
	return NULL;
}

#endif //SNAPSTORE
