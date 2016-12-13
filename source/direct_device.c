#include "stdafx.h"
#include "container.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"
#include "direct_device.h"

typedef struct direct_device_s
{
	content_t content;

	dev_t dev_id;

	page_array_t* arr;
	size_t buff_size;
	struct block_device* blk_dev;
}direct_device_t;

container_t gDirectDevices;

//////////////////////////////////////////////////////////////////////////
direct_device_t* _direct_device_find( dev_t dev_id )
{
	direct_device_t* dir_dev = NULL;
	content_t* content = NULL;

	CONTAINER_FOREACH_BEGIN( gDirectDevices, content ){
		dir_dev = (direct_device_t*)content;
		if (((direct_device_t*)content)->dev_id == dev_id)
			dir_dev = (direct_device_t*)content;
	}CONTAINER_FOREACH_END( gDirectDevices );

	return dir_dev;
}
//////////////////////////////////////////////////////////////////////////
void _direct_device_close( direct_device_t* dir_dev )
{
	if (dir_dev->arr != NULL){
		page_array_free( dir_dev->arr );
		dir_dev->arr = NULL;
	}
	if (dir_dev->blk_dev != NULL){
		blk_dev_close( dir_dev->blk_dev );
		dir_dev->blk_dev = NULL;
	}
	dir_dev->dev_id = 0;
}
//////////////////////////////////////////////////////////////////////////
int direct_device_init( void )
{
	return container_init( &gDirectDevices, sizeof( direct_device_t ), NULL );
}
//////////////////////////////////////////////////////////////////////////
int direct_device_done( void )
{
	int result = SUCCESS;
	content_t* content;

	while (NULL != (content = container_get_first( &gDirectDevices ))){
		_direct_device_close( (direct_device_t*)content );

		content_free( content );
	}

	if (result == SUCCESS)
		result = container_done( &gDirectDevices );
	return result;
}
//////////////////////////////////////////////////////////////////////////
int direct_device_open( dev_t dev_id )
{
	int res;
	direct_device_t* dir_dev;

	dir_dev = _direct_device_find( dev_id );
	if (NULL != dir_dev){
		return -EBUSY;
	}

	dir_dev = (direct_device_t*)content_new( &gDirectDevices );

	dir_dev->buff_size = (512 * 1024);
	dir_dev->arr = page_array_alloc( dir_dev->buff_size / PAGE_SIZE, GFP_KERNEL );
	if (dir_dev->arr == NULL){
		content_free( &dir_dev->content );
		return -ENOMEM;
	}

	res = blk_dev_open( dev_id, &dir_dev->blk_dev );
	if (res != SUCCESS){
		_direct_device_close( dir_dev );
		content_free( &dir_dev->content );
		return res;
	}

	dir_dev->dev_id = dev_id;
	container_push_back( &gDirectDevices, &dir_dev->content );
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
int direct_device_close( dev_t dev_id )
{
	direct_device_t* dir_dev = NULL;

	dir_dev = _direct_device_find( dev_id );
	if (dir_dev == NULL){
		log_errorln_dev_t( "Cannot find device for closing. dev_id=", dev_id );
		return -ENODEV;
	}

	_direct_device_close( dir_dev );
	container_free( &dir_dev->content );
	return SUCCESS;

}
//////////////////////////////////////////////////////////////////////////
int direct_device_read4user( dev_t dev_id, stream_size_t offset, char* user_buffer, size_t length )
{
	int res = SUCCESS;
	size_t local_ofs = 0;
	direct_device_t* dir_dev = NULL;

	dir_dev = _direct_device_find( dev_id );
	if (dir_dev == NULL){
		log_errorln_dev_t( "Cannot find device for closing. dev_id=", dev_id );
		return -ENODEV;
	}

	while (local_ofs < length){
		size_t local_size = min_t( size_t, length - local_ofs, dir_dev->buff_size );

		sector_t ofs_sect = sector_from_streamsize( offset + local_ofs );
		sector_t len_sect = sector_from_size( local_size );
		sector_t processed_sect;

		processed_sect = blk_dev_direct_submit_pages( dir_dev->blk_dev, READ, 0, dir_dev->arr, ofs_sect, len_sect );

		if (processed_sect != len_sect){
			log_errorln( "Failed to read direct device.");
			log_errorln_sect( "ofs_sect=", ofs_sect );
			log_errorln_sect( "len_sect=", len_sect );
			log_errorln_sect( "processed_sect=", processed_sect );
			if (processed_sect <= 0){
				res = -EINVAL;
				break;
			}
		}

		if (local_size != page_array_copy2user( user_buffer + local_ofs, 0, dir_dev->arr, local_size )){
			log_errorln_lld( "Failed to read data by offset=", offset+local_ofs );
			return -ENODATA;
		}

		local_ofs += local_size;
	}
	return res;
}
//////////////////////////////////////////////////////////////////////////