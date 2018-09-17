#pragma once

#ifndef SNAPSTORE

#include "snapshotdata_shared.h"
#include "snapshotdata_blkinfo.h"
#include "snapshotdata_buffer.h"

typedef struct snapshotdata_memory_s{
	snapshotdata_shared_t shared;

	snapshotdata_blkinfo_t blkinfo;

	snapshotdata_buffer_t buffer;
}snapshotdata_memory_t;

int snapshotdata_memory_Init( void );
int snapshotdata_memory_Done( void );

//void snapshotdata_memory_free( snapshotdata_memory_t* mem );
snapshotdata_memory_t* snapshotdata_memory_create( uuid_t* id, size_t buffer_size );

snapshotdata_memory_t* snapshotdata_memory_find( uuid_t* id );

#endif //SNAPSTORE
