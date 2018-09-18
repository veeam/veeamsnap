#pragma once

#ifndef SNAPSTORE

#include "snapshotdata_shared.h"
#include "snapshotdata_blkinfo.h"
#include "snapshotdata_file.h"

typedef struct snapshotdata_common_s{
	snapshotdata_shared_t shared;

	snapshotdata_blkinfo_t blkinfo;

	snapshotdata_file_t file;
}snapshotdata_common_t;

int snapshotdata_common_Init( void );
int snapshotdata_common_Done( void );

//void snapshotdata_common_free( snapshotdata_common_t* comm_disk );
snapshotdata_common_t* snapshotdata_common_create( veeam_uuid_t* id, dev_t dev_id );

snapshotdata_common_t* snapshotdata_common_find( veeam_uuid_t* id );

#endif //SNAPSTORE
