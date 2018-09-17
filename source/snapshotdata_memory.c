#include "stdafx.h"
#include "snapshotdata_memory.h"

#ifndef SNAPSTORE

static container_t SnapshotMemories;

int snapshotdata_memory_Init( void )
{
	return container_init( &SnapshotMemories, sizeof( snapshotdata_memory_t ) );
}

void snapshotdata_memory_free( snapshotdata_memory_t* mem )
{
	snapshotdata_buffer_destroy( &mem->buffer );
}

void snapshotdata_memory_free_cb( void* this_resource )
{
	snapshotdata_memory_t* mem = (snapshotdata_memory_t*)this_resource;
	snapshotdata_memory_free( mem );
	container_free( &mem->shared.content );
}

int snapshotdata_memory_Done( void )
{
	int res = SUCCESS;
	content_t* content;

	//container
	content = container_get_first( &SnapshotMemories );
	while (NULL != content){
		snapshotdata_memory_t* mem = (snapshotdata_memory_t*)content;
		uuid_t* id = &(mem->shared.unique_id);

		log_errorln_uuid( "Cleanup. id=", id );
		log_errorln_d( "owners count=", snapshotdata_shared_own_cnt( &mem->shared ) );

		snapshotdata_memory_free( mem );
		content_free( content );

		content = container_get_first( &SnapshotMemories );
	}
	if (SUCCESS != (res = container_done( &SnapshotMemories ))){
		log_errorln( "Container is not empty" );
	};
	return res;
}

snapshotdata_memory_t* snapshotdata_memory_create( uuid_t* id, size_t buffer_size )
{
	int res = SUCCESS;
	snapshotdata_memory_t* mem = NULL;

	log_traceln_uuid( "id=", id );
	log_traceln_sz( "size=", buffer_size );

	mem = (snapshotdata_memory_t*)content_new( &SnapshotMemories );
	if (mem == NULL)
		return NULL;

	snapshotdata_blkinfo_create( &mem->blkinfo, sector_from_size(buffer_size) );
	res = snapshotdata_buffer_create( &mem->buffer, buffer_size );
	if (SUCCESS != res){
		content_free( &mem->shared.content );
		return NULL;
	}

	snapshotdata_shared_create( &mem->shared, id, SNAPSHOTDATA_TYPE_MEM, snapshotdata_memory_free_cb );

	container_push_back( &SnapshotMemories, &mem->shared.content );
	log_traceln( "New snapshotdata memory storage create" );

	return mem;
}

snapshotdata_memory_t* snapshotdata_memory_find( uuid_t* id )
{
	return (snapshotdata_memory_t*)snapshotdata_shared_find( id, &SnapshotMemories );
}


#endif //SNAPSTORE

