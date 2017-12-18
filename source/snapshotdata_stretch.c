#include "stdafx.h"
#include "snapshotdata_stretch.h"

#ifndef SNAPSTORE

static container_t SnapshotStretchDisks;

int snapshotdata_stretch_Init( void )
{
	return container_init( &SnapshotStretchDisks, sizeof( snapshotdata_stretch_t ));
}

int snapshotdata_stretch_Done( void )
{
	int res = SUCCESS;
	content_t* content;

	CONTAINER_FOREACH_BEGIN( SnapshotStretchDisks, content )
	{
		snapshotdata_stretch_t* stretch = (snapshotdata_stretch_t*)content;
		uuid_t* id = &(stretch->shared.unique_id);

		log_errorln_uuid( "Still in use. id=", id );
		log_errorln_dev_t( "device=", stretch->file.blk_dev_id );
		log_errorln_d( "owners count=", snapshotdata_shared_own_cnt( &stretch->shared ) );

		//??? - cleanup is not realized

	}CONTAINER_FOREACH_END( SnapshotStretchDisks );

	if (SUCCESS != (res = container_done( &SnapshotStretchDisks ))){
		log_errorln( "Container is not empty" );
	};
	return res;
}

void snapshotdata_stretch_free( snapshotdata_stretch_t* stretch )
{
	uuid_t* id = &(stretch->shared.unique_id);
	log_traceln_uuid( "id=", id );

	snapshotdata_stretch_terminate( stretch );
	{
		ctrl_pipe_t* pipe = stretch->ctrl_pipe;
		stretch->ctrl_pipe = NULL;

		ctrl_pipe_put_resource( pipe );
	}
	snapshotdata_file_destroy( &stretch->file );
}

void snapshotdata_stretch_free_cb( void* this_resource )
{
	snapshotdata_stretch_t* stretch_disk = (snapshotdata_stretch_t*)this_resource;

	snapshotdata_stretch_free( stretch_disk );

	container_free( &stretch_disk->shared.content );
}


snapshotdata_stretch_t* snapshotdata_stretch_create( uuid_t* id, dev_t dev_id )
{
	int res = SUCCESS;
	snapshotdata_stretch_t* stretch = NULL;

	log_traceln_uuid( "id=", id );
	log_traceln_dev_t( "dev_id=", dev_id );

	stretch = (snapshotdata_stretch_t*)content_new( &SnapshotStretchDisks );
	if (stretch == NULL)
		return NULL;


	res = snapshotdata_file_create( &stretch->file, dev_id );
	if (SUCCESS == res){

		stretch->empty_limit = (sector_t)(64 * 1024 * 1024 / SECTOR512); //by default value
		stretch->halffilled = false;
		stretch->overflowed = false;

		snapshotdata_blkinfo_create( &stretch->blkinfo, 0 );
		snapshotdata_shared_create( &stretch->shared, id, SNAPSHOTDATA_TYPE_STRETCH, snapshotdata_stretch_free_cb );

		container_push_back( &SnapshotStretchDisks, &stretch->shared.content );

		log_traceln( "New snapshot stretch added to container." );
	} else{
		snapshotdata_file_destroy( &stretch->file );

		content_free( &stretch->shared.content );
		stretch = NULL;
	}

	return stretch;
}

snapshotdata_stretch_t* snapshotdata_stretch_find( uuid_t* id )
{
	return (snapshotdata_stretch_t*)snapshotdata_shared_find( id, &SnapshotStretchDisks );
}

void snapshotdata_stretch_halffill( snapshotdata_stretch_t* stretch_disk, ssize_t fill_status )
{
	if (!stretch_disk->halffilled){
		ctrl_pipe_request_halffill( stretch_disk->ctrl_pipe, fill_status );

		stretch_disk->halffilled = true;
	}
}

void snapshotdata_stretch_check_halffill( snapshotdata_stretch_t* stretch, snapshotdata_blkinfo_t* blkinfo )
{
	if ((blkinfo->limit - blkinfo->pos) < stretch->empty_limit)
		snapshotdata_stretch_halffill( stretch, sector_to_streamsize( blkinfo->pos ) );
}

void snapshotdata_stretch_overflow( snapshotdata_stretch_t* stretch_disk, unsigned int error_code, snapshotdata_blkinfo_t* blkinfo )
{
	if (!stretch_disk->overflowed){
		ctrl_pipe_request_overflow( stretch_disk->ctrl_pipe, error_code, sector_to_streamsize( blkinfo->pos ) );

		stretch_disk->overflowed = true;
	}
}

void snapshotdata_stretch_terminate( snapshotdata_stretch_t* stretch_disk )
{
	ctrl_pipe_request_terminate( stretch_disk->ctrl_pipe, -1 );
}

#endif //SNAPSTORE
