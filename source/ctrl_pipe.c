#include "stdafx.h"
#include <linux/poll.h>

#include "veeamsnap_ioctl.h"
#include "version.h"
#include "container.h"
#include "container_spinlocking.h"
#include "tracking.h"
#include "snapshot.h"
#include "range.h"
#include "rangeset.h"
#include "rangelist.h"
#include "rangevector.h"
#include "sparse_array_1lv.h"
#include "queue_spinlocking.h"
#include "blk_dev_utile.h"
#include "shared_resource.h"
#include "ctrl_pipe.h"
#include "snapshotdata.h"


typedef struct cmd_to_user_s
{
	content_t content;
	char* request_buffer;
	size_t request_size;//in bytes
}cmd_to_user_t;

size_t ctrl_pipe_command_initiate( ctrl_pipe_t* pipe, char *buffer, size_t length );
size_t ctrl_pipe_command_next_portion( ctrl_pipe_t* pipe, char *buffer, size_t length );


void ctrl_pipe_request_acknowledge( ctrl_pipe_t* pipe, unsigned int result );
void ctrl_pipe_request_invalid( ctrl_pipe_t* pipe );


container_t CtrlPipes;


void ctrl_pipe_init( void )
{
	log_traceln( "." );
	container_init( &CtrlPipes, sizeof( ctrl_pipe_t ), NULL );
}

void ctrl_pipe_done( void )
{
	log_traceln( "." );
	container_done( &CtrlPipes );
}

void ctrl_pipe_release_cb( void* resource )
{
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)resource;

	log_traceln( "." );

	while (!container_empty( &pipe->cmd_to_user )){
		content_t* content = container_get_first( &pipe->cmd_to_user );
		content_free( content );
	}

	container_done( &pipe->cmd_to_user );

	container_free( &pipe->content );
}

ctrl_pipe_t* ctrl_pipe_new( void )
{
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)container_new( &CtrlPipes );
	log_traceln( "." );

	container_init( &pipe->cmd_to_user, sizeof( cmd_to_user_t ), NULL );

	shared_resource_init( &pipe->sharing_header, pipe, ctrl_pipe_release_cb );

	init_waitqueue_head( &pipe->readq );

	return pipe;
}

size_t ctrl_pipe_read( ctrl_pipe_t* pipe, char __user *buffer, size_t length )
{
	size_t processed = 0;
	cmd_to_user_t* cmd_to_user = NULL;

	log_traceln( "." );

	if (container_empty( &pipe->cmd_to_user )){ //nothing to read
		if (wait_event_interruptible( pipe->readq, !container_empty( &pipe->cmd_to_user ) )){
			log_errorln( "Failed to wait pipe read queue" );
			return -ERESTARTSYS;
		}
	};

	cmd_to_user = (cmd_to_user_t*)container_get_first( &pipe->cmd_to_user );
	if (cmd_to_user == NULL){
		log_errorln( "Failed to wait pipe read queue" );
		return -ERESTARTSYS;
	}

	do {
		if (length < cmd_to_user->request_size){
			log_errorln_sz( "user buffer too small. request length =", cmd_to_user->request_size );
			processed = -ENODATA;
			break;
		}

		if (0 != copy_to_user( buffer, cmd_to_user->request_buffer, cmd_to_user->request_size )){
			log_errorln( "Invalid user buffer" );
			processed = -EINVAL;
			break;
		}

		processed = cmd_to_user->request_size;
	} while (false);


	if (processed > 0){
		dbg_kfree( cmd_to_user->request_buffer );
		content_free( &cmd_to_user->content );
	}
	else
		container_push_top( &pipe->cmd_to_user, &cmd_to_user->content ); //push to top of queue

	return processed;
}

size_t ctrl_pipe_write( ctrl_pipe_t* pipe, char *buffer, size_t length )
{
	size_t processed = 0;
	log_traceln_sz( "length=", length );

	do{
		unsigned int command;

		if ((length - processed) < 4){
			log_errorln_sz( "Invalid command length=", length);
			break;
		}
		command = *(unsigned int*)buffer;
		processed += sizeof( unsigned int );

		switch (command){
		case VEEAMSNAP_CHARCMD_INITIATE:
			processed += ctrl_pipe_command_initiate( pipe, buffer + processed, length - processed );
			break;
		case VEEAMSNAP_CHARCMD_NEXT_PORTION:
			processed += ctrl_pipe_command_next_portion( pipe, buffer + processed, length - processed );
			break;
		default:
			log_errorln_x( "Invalid command=", command );
			break;
		}
	} while (false);

	return processed;
}

unsigned int ctrl_pipe_poll( ctrl_pipe_t* pipe )
{
	unsigned int mask = 0;

	if (!container_empty( &pipe->cmd_to_user )){
		mask |= (POLLIN | POLLRDNORM);     /* readable */

		log_traceln( "POLLIN" );
	}
	mask |= (POLLOUT | POLLWRNORM);   /* writable */

	return mask;
}


size_t ctrl_pipe_command_initiate( ctrl_pipe_t* pipe, char *buffer, size_t length )
{
	unsigned int result = SUCCESS;
	size_t processed = 0;

	//log_traceln( "." );

	do{
		stream_size_t stretch_empty_limit;
		unsigned int dev_id_list_length;
		unsigned int dev_id_list_inx;
		unsigned char* unique_id;
		struct ioctl_dev_id_s* snapshotdata_dev_id;
		struct ioctl_dev_id_s* dev_id_list;
		snapshotdata_stretch_disk_t* stretch_disk = NULL;

		//get snapshotdata id
		if ((length - processed) < 16){
			log_errorln_sz( "Failed to get snapshotdata id. length=", length );
			break;
		}
		unique_id = (unsigned char*)(buffer + processed);
		processed += 16;
		log_traceln_uuid( "unique_id=", unique_id );

		//get snapshotdata empty limit
		if ((length - processed) < sizeof( stream_size_t )){
			log_errorln_sz( "Failed to get snapshotdata device id. length=", length );
			break;
		}
		stretch_empty_limit = *(stream_size_t*)(buffer + processed);
		processed += sizeof( stream_size_t );
		log_traceln_lld( "stretch_empty_limit=", stretch_empty_limit );

		//get snapshotdata device id
		if ((length - processed) < sizeof( struct ioctl_dev_id_s )){
			log_errorln_sz( "Failed to get snapshotdata device id. length=", length );
			break;
		}
		snapshotdata_dev_id = (struct ioctl_dev_id_s*)(buffer + processed);
		processed += sizeof( struct ioctl_dev_id_s );
		log_traceln_dev_t( "snapshotdata_dev_id=", MKDEV( snapshotdata_dev_id->major, snapshotdata_dev_id->minor ) );

		//get device id list length
		if ((length - processed) < 4){
			log_errorln_sz( "Failed to get device id list length. length=", length );
			break;
		}
		dev_id_list_length = *(unsigned int*)(buffer + processed);
		processed += sizeof( unsigned int );
		log_traceln_d( "dev_id_list_length=", dev_id_list_length );

		//get devices id list
		if ((length - processed) < (dev_id_list_length*sizeof( struct ioctl_dev_id_s ))){
			log_errorln_sz( "Failed to get all device from device id list length. length=", length );
			break;
		}
		dev_id_list = (struct ioctl_dev_id_s*)(buffer + processed);
		processed += (dev_id_list_length*sizeof( struct ioctl_dev_id_s ));

		for (dev_id_list_inx = 0; dev_id_list_inx < dev_id_list_length; ++dev_id_list_inx){
			log_traceln_dev_id_s( "dev_id=", dev_id_list[dev_id_list_inx] );
		}

		stretch_disk = snapshotdata_stretch_create( unique_id, MKDEV( snapshotdata_dev_id->major, snapshotdata_dev_id->minor ) );
		if (stretch_disk == NULL){
			result = -ENODATA;
			log_errorln( "Failed to create stretch snapshot data" );
			break;
		}

		stretch_disk->ctrl_pipe = ctrl_pipe_get_resource( pipe );

		stretch_disk->stretch_empty_limit = sector_from_streamsize(stretch_empty_limit);

		for (dev_id_list_inx = 0; dev_id_list_inx < dev_id_list_length; ++dev_id_list_inx){

			dev_t dev_id = MKDEV( dev_id_list[dev_id_list_inx].major, dev_id_list[dev_id_list_inx].minor );

			result = snapshotdata_stretch_add_dev( stretch_disk, dev_id );
			if (result != SUCCESS){
				log_errorln_dev_t( "Failed to add device to stretch snapshotdata. device=", dev_id );
				break;
			}
		}
		if (result != SUCCESS){
			snapshotdata_stretch_free( stretch_disk );
		}

	} while (false);
	ctrl_pipe_request_acknowledge( pipe, result );

	return processed;
}

size_t ctrl_pipe_command_next_portion( ctrl_pipe_t* pipe, char *buffer, size_t length )
{
	unsigned int result = SUCCESS;
	size_t processed = 0;
	//log_traceln( "." );
	do{
		unsigned char* unique_id;
		unsigned int ranges_length;
		unsigned int ranges_inx;
		struct ioctl_range_s* ranges;
		snapshotdata_stretch_disk_t* stretch_disk = NULL;

		//get snapshotdata id
		if ((length - processed) < 16){
			log_errorln_sz( "Failed to get snapshotdata id. length=", length );
			break;
		}
		unique_id = (unsigned char*)(buffer + processed);
		processed += 16;
		log_traceln_uuid( "snapshotdata unique_id=", unique_id );

		//get ranges length
		if ((length - processed) < 4){
			log_errorln_sz( "Failed to get device id list length. length=", length );
			break;
		}
		ranges_length = *(unsigned int*)(buffer + processed);
		processed += sizeof( unsigned int );

		// ranges
		if ((length - processed) < (ranges_length*sizeof( struct ioctl_range_s ))){
			log_errorln_sz( "Failed to get all device from device id list length. length=", length );
			break;
		}
		ranges = (struct ioctl_range_s*)(buffer + processed);
		processed += (ranges_length*sizeof( struct ioctl_range_s ));


		stretch_disk = snapshotdata_stretch_get( unique_id );
		if (stretch_disk == NULL){
			log_errorln( "Cannot find stretch snapshot data" );
			result = -ENODEV;
			break;
		}

		for (ranges_inx = 0; ranges_inx < ranges_length; ++ranges_inx){
			int res = SUCCESS;
			range_t range;
			range.ofs = sector_from_streamsize( ranges[ranges_inx].left );
			range.cnt = sector_from_streamsize( ranges[ranges_inx].right ) - range.ofs;

			res = snapshotdata_stretch_add_range( stretch_disk, &range );
			if (res != SUCCESS){
				log_errorln( "Cannot add range" );
				result = res;
				break;
			}
		}
#ifdef SNAPDATA_ZEROED
		if (get_zerosnapdata()){
			int res = SUCCESS;
			snapshotdata_t* snapshotdata;
			res = snapshotdata_FindByDevId( stretch_disk->stretch_blk_dev_id, &snapshotdata );
			if (res != SUCCESS){
				log_traceln_dev_t( "Cannot set zero sectors for device=", stretch_disk->stretch_blk_dev_id );
				break;
			}

			//log_traceln_d( "Zero sectors set. ranges: ", ranges_length );
			for (ranges_inx = 0; ranges_inx < ranges_length; ++ranges_inx){
				int res = SUCCESS;
				range_t range;
				range.ofs = sector_from_streamsize( ranges[ranges_inx].left );
				range.cnt = sector_from_streamsize( ranges[ranges_inx].right ) - range.ofs;

				//log_traceln_range( "range:", range );
				res = rangevector_add( &snapshotdata->zero_sectors, range );
				if (res != SUCCESS){
					log_errorln( "Cannot add range to zero sectors" );
					result = res;
					break;
				}
			}
			rangevector_sort( &snapshotdata->zero_sectors );
		}
#endif

	} while (false);

	return processed;
}

void ctrl_pipe_push_request( ctrl_pipe_t* pipe, unsigned int* cmd, size_t cmd_len )
{
	cmd_to_user_t* request = NULL;

	log_traceln( "." );

	request = (cmd_to_user_t*)content_new( &pipe->cmd_to_user );
	if (request == NULL){
		log_errorln( "Failed to create command acknowledge." );
		dbg_kfree( cmd );
		return;
	}

	request->request_size = cmd_len * sizeof( unsigned int );
	request->request_buffer = (char*)cmd;
	container_push_back( &pipe->cmd_to_user, &request->content );

	wake_up( &pipe->readq );
}

void ctrl_pipe_request_acknowledge( ctrl_pipe_t* pipe, unsigned int result )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 2;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = VEEAMSNAP_CHARCMD_ACKNOWLEDGE;
	cmd[1] = result;

	ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_halffill( ctrl_pipe_t* pipe, unsigned long long filled_status )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 3;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_HALFFILL;
	cmd[1] = (filled_status & 0xFFFFffff); //lo
	cmd[2] = (filled_status >> 32);

	ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_overflow( ctrl_pipe_t* pipe, unsigned int error_code )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 2;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_OVERFLOW;
	cmd[1] = error_code;

	ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_terminate( ctrl_pipe_t* pipe )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 1;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_TERMINATE;

	ctrl_pipe_push_request( pipe, cmd, cmd_len );

}

void ctrl_pipe_request_invalid( ctrl_pipe_t* pipe )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 1;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_INVALID;

	ctrl_pipe_push_request( pipe, cmd, cmd_len );
}


