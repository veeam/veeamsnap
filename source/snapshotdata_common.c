#include "stdafx.h"
#include "snapshotdata_common.h"

#ifndef SNAPSTORE

static container_t SnapshotCommonDisks;

int snapshotdata_common_Init( void )
{
	return container_init( &SnapshotCommonDisks, sizeof( snapshotdata_common_t ) );
}

void snapshotdata_common_free( snapshotdata_common_t* common )
{
	uuid_t* id = &(common->shared.unique_id);
	log_traceln_uuid( "id=", id );
	snapshotdata_file_destroy( &common->file );
}

void snapshotdata_common_free_cb( void* this_resource )
{
	snapshotdata_common_t* common = (snapshotdata_common_t*)this_resource;
	snapshotdata_common_free( common );
	container_free( &common->shared.content );
}

int snapshotdata_common_Done( void )
{
	int res;
	content_t* content;

	//container
	content = container_get_first( &SnapshotCommonDisks );
	while (NULL != content){
		snapshotdata_common_t* common = (snapshotdata_common_t*)content;
		uuid_t* id = &(common->shared.unique_id);

		log_errorln_uuid( "Cleanup. id=", id );
		log_errorln_dev_t( "device=", common->file.blk_dev_id );
		log_errorln_d( "owners count=", snapshotdata_shared_own_cnt( &common->shared) );

		snapshotdata_common_free( common );
		content_free( content );

		content = container_get_first( &SnapshotCommonDisks );
	}
	if (SUCCESS != (res = container_done( &SnapshotCommonDisks ))){
		log_errorln( "Container is not empty" );
	};
	return res;
}

snapshotdata_common_t* snapshotdata_common_create( uuid_t* id, dev_t dev_id )
{
	int res = SUCCESS;
	snapshotdata_common_t* common = NULL;

	log_traceln_uuid( "id=", id );
	log_traceln_dev_t( "dev_id=", dev_id );

	common = (snapshotdata_common_t*)content_new( &SnapshotCommonDisks );
	if (common == NULL)
		return NULL;

	snapshotdata_blkinfo_create( &common->blkinfo, 0 );
	res = snapshotdata_file_create( &common->file, dev_id );
	if (SUCCESS != res){
		content_free( &common->shared.content );
		return NULL;
	}

	snapshotdata_shared_create( &common->shared, id, SNAPSHOTDATA_TYPE_COMMON, snapshotdata_common_free_cb );

	container_push_back( &SnapshotCommonDisks, &common->shared.content );
	log_traceln( "New snapshotdata common storage create" );

	return common;
}

snapshotdata_common_t* snapshotdata_common_find( uuid_t* id )
{
	return (snapshotdata_common_t*)snapshotdata_shared_find( id, &SnapshotCommonDisks );
}

#endif //SNAPSTORE
