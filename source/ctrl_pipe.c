#include "stdafx.h"
#include <linux/poll.h>
#include "ctrl_pipe.h"
#include "version.h"
#include "veeamsnap_ioctl.h"
#include "uuid_util.h"

#ifdef SNAPSTORE
#include "snapstore.h"
#else
#include "snapshotdata_stretch.h"
#include "snapshotdata.h"
#endif

#include "zerosectors.h"

typedef struct cmd_to_user_s
{
	content_t content;
	char* request_buffer;
	size_t request_size;//in bytes
}cmd_to_user_t;

ssize_t ctrl_pipe_command_initiate( ctrl_pipe_t* pipe, const char __user *buffer, size_t length );
ssize_t ctrl_pipe_command_next_portion( ctrl_pipe_t* pipe, const char __user *buffer, size_t length );


void ctrl_pipe_request_acknowledge( ctrl_pipe_t* pipe, unsigned int result );
void ctrl_pipe_request_invalid( ctrl_pipe_t* pipe );


container_t CtrlPipes;


void ctrl_pipe_init( void )
{
	log_traceln( "." );
	container_init( &CtrlPipes, sizeof( ctrl_pipe_t ) );
}

void ctrl_pipe_done( void )
{
	log_traceln( "." );
	if (SUCCESS != container_done( &CtrlPipes )){
		log_errorln( "Container is not empty" );
	};
}

void ctrl_pipe_release_cb( void* resource )
{
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)resource;

	//log_traceln( "." );

	while (!container_empty( &pipe->cmd_to_user )){
		cmd_to_user_t* request = (cmd_to_user_t*)container_get_first( &pipe->cmd_to_user );
		dbg_kfree( request->request_buffer );

		content_free( &request->content );
	}

	if (SUCCESS != container_done( &pipe->cmd_to_user )){
		log_errorln( "Container is not empty" );
	};

	container_free( &pipe->content );
}

ctrl_pipe_t* ctrl_pipe_new( void )
{
	ctrl_pipe_t* pipe = (ctrl_pipe_t*)container_new( &CtrlPipes );
	//log_traceln( "." );

	container_init( &pipe->cmd_to_user, sizeof( cmd_to_user_t ) );

	shared_resource_init( &pipe->sharing_header, pipe, ctrl_pipe_release_cb );

	init_waitqueue_head( &pipe->readq );

	return pipe;
}

ssize_t ctrl_pipe_read( ctrl_pipe_t* pipe, char __user *buffer, size_t length )
{
	ssize_t processed = 0;
	cmd_to_user_t* cmd_to_user = NULL;

	//log_traceln( "." );

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

ssize_t ctrl_pipe_write( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
{
	ssize_t processed = 0;
	//log_traceln_sz( "length=", length );

	do{
		unsigned int command;

		if ((length - processed) < 4){
			log_errorln_sz( "Invalid command length=", length);
			break;
		}
		if (0 != copy_from_user( &command, buffer + processed, sizeof( unsigned int ) )){
			log_errorln( "Failed to write to pipe. Invalid user buffer" );
			processed = -EINVAL;
			break;
		}
		processed += sizeof( unsigned int );
		//+4
		switch (command){
		case VEEAMSNAP_CHARCMD_INITIATE:
		{
			ssize_t res = ctrl_pipe_command_initiate( pipe, buffer + processed, length - processed );
			if (res >= 0)
				processed += res;
			else
				processed = res;
		}
			break;
		case VEEAMSNAP_CHARCMD_NEXT_PORTION:
		{
			ssize_t res = ctrl_pipe_command_next_portion( pipe, buffer + processed, length - processed );
			if (res >= 0)
				processed += res;
			else
				processed = res;
		}
			break;
		default:
			log_errorln_x( "Invalid command=", command );
			break;
		}
	} while (false);

	//log_traceln( "complete");
	return processed;
}

unsigned int ctrl_pipe_poll( ctrl_pipe_t* pipe )
{
	unsigned int mask = 0;

	if (!container_empty( &pipe->cmd_to_user )){
		mask |= (POLLIN | POLLRDNORM);     /* readable */

		//log_traceln( "POLLIN" );
	}
	mask |= (POLLOUT | POLLWRNORM);   /* writable */

	return mask;
}


ssize_t ctrl_pipe_command_initiate( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
{
	int result = SUCCESS;
	ssize_t processed = 0;

	char* kernel_buffer = dbg_kmalloc( length, GFP_KERNEL );
	if (kernel_buffer == NULL){
		log_errorln_sz( "Failed to process next portion to pipe. Cannot allocate buffer. length=", length );
		return -ENOMEM;
	}

	if (0 != copy_from_user( kernel_buffer, buffer, length )){
		dbg_kfree( kernel_buffer );
		log_errorln( "Failed to write to pipe. Invalid user buffer" );
		return -EINVAL;
	}

	do{
		stream_size_t stretch_empty_limit;
		unsigned int dev_id_list_length;
		unsigned int dev_id_list_inx;
		veeam_uuid_t* unique_id;
		struct ioctl_dev_id_s* snapshotdata_dev_id;
		struct ioctl_dev_id_s* dev_id_list;

		//get snapshotdata id
		if ((length - processed) < 16){
			log_errorln_sz( "Failed to get snapshotdata id. length=", length );
			break;
		}
		unique_id = (veeam_uuid_t*)(kernel_buffer + processed);
		processed += 16;
		log_traceln_uuid( "unique_id=", unique_id );

		//get snapshotdata empty limit
		if ((length - processed) < sizeof( stream_size_t )){
			log_errorln_sz( "Failed to get snapshotdata device id. length=", length );
			break;
		}
		stretch_empty_limit = *(stream_size_t*)(kernel_buffer + processed);
		processed += sizeof( stream_size_t );
		log_traceln_lld( "stretch_empty_limit=", stretch_empty_limit );

		//get snapshotdata device id
		if ((length - processed) < sizeof( struct ioctl_dev_id_s )){
			log_errorln_sz( "Failed to get snapshotdata device id. length=", length );
			break;
		}
		snapshotdata_dev_id = (struct ioctl_dev_id_s*)(kernel_buffer + processed);
		processed += sizeof( struct ioctl_dev_id_s );
		log_traceln_dev_t( "snapshotdata_dev_id=", MKDEV( snapshotdata_dev_id->major, snapshotdata_dev_id->minor ) );

		//get device id list length
		if ((length - processed) < 4){
			log_errorln_sz( "Failed to get device id list length. length=", length );
			break;
		}
		dev_id_list_length = *(unsigned int*)(kernel_buffer + processed);
		processed += sizeof( unsigned int );
		log_traceln_d( "dev_id_list_length=", dev_id_list_length );

		//get devices id list
		if ((length - processed) < (dev_id_list_length*sizeof( struct ioctl_dev_id_s ))){
			log_errorln_sz( "Failed to get all device from device id list length. length=", length );
			break;
		}
		dev_id_list = (struct ioctl_dev_id_s*)(kernel_buffer + processed);
		processed += (dev_id_list_length*sizeof( struct ioctl_dev_id_s ));

		for (dev_id_list_inx = 0; dev_id_list_inx < dev_id_list_length; ++dev_id_list_inx){
			log_traceln_dev_id_s( "dev_id=", dev_id_list[dev_id_list_inx] );
		}
#ifdef SNAPSTORE
		{
			size_t inx;
			dev_t* dev_set;
			size_t dev_id_set_length = (size_t)dev_id_list_length;
			dev_t snapstore_dev_id = MKDEV( snapshotdata_dev_id->major, snapshotdata_dev_id->minor );

			dev_set = dbg_kzalloc( sizeof( dev_t ) * dev_id_set_length, GFP_KERNEL );
			if (NULL == dev_set){
				log_errorln( "Cannot allocate memory" );
				result = -ENOMEM;
				break;
			}

			for (inx = 0; inx < dev_id_set_length; ++inx)
				dev_set[inx] = MKDEV( dev_id_list[inx].major, dev_id_list[inx].minor );

			result = snapstore_create( unique_id, snapstore_dev_id, dev_set, dev_id_set_length );
			dbg_kfree( dev_set );
			if (result != SUCCESS){
				log_errorln_dev_t( "Failed to create snapstore on device=", snapstore_dev_id );
				break;
			}

			result = snapstore_stretch_initiate( unique_id, pipe, sector_from_streamsize( stretch_empty_limit ) );
			if (result != SUCCESS){
				log_errorln_uuid( "Failed to initiate stretch snapstore ", unique_id );
				break;
			}
		}
#else //SNAPSTORE
		{
			snapshotdata_stretch_t* stretch = NULL;
			stretch = snapshotdata_stretch_create( unique_id, MKDEV( snapshotdata_dev_id->major, snapshotdata_dev_id->minor ) );
			if (stretch == NULL){
				result = -ENODATA;
				log_errorln( "Failed to create stretch snapshot data" );
				break;
			}

			stretch->ctrl_pipe = ctrl_pipe_get_resource( pipe );

			stretch->empty_limit = sector_from_streamsize( stretch_empty_limit );

			for (dev_id_list_inx = 0; dev_id_list_inx < dev_id_list_length; ++dev_id_list_inx){

				dev_t dev_id = MKDEV( dev_id_list[dev_id_list_inx].major, dev_id_list[dev_id_list_inx].minor );

				result = snapshotdata_add_dev( &stretch->shared, dev_id );
				if (result != SUCCESS){
					log_errorln_dev_t( "Failed to add device to stretch snapshotdata. device=", dev_id );
					break;
				}
			}
			if (result != SUCCESS){
				snapshotdata_stretch_free( stretch );
			}
		}
#endif //SNAPSTORE

	} while (false);
	dbg_kfree( kernel_buffer );
	ctrl_pipe_request_acknowledge( pipe, result );
	
	if (result == SUCCESS)
		return processed;
	return result;
}

ssize_t ctrl_pipe_command_next_portion( ctrl_pipe_t* pipe, const char __user *buffer, size_t length )
{
	int result = SUCCESS;
	ssize_t processed = 0;
	page_array_t* ranges = NULL;

	do{
		veeam_uuid_t unique_id;
		unsigned int ranges_length;
		size_t ranges_buffer_size;

		//get snapshotdata id
		if ((length - processed) < 16){
			log_errorln_sz( "Failed to get snapshotdata id. length=", length );
			break;
		}
		if (0 != copy_from_user( &unique_id, buffer + processed, sizeof( veeam_uuid_t ) )){
			log_errorln( "Failed to write to pipe. Invalid user buffer" );
			processed = -EINVAL;
			break;
		}
		processed += 16;
		log_traceln_uuid( "snapshotdata unique_id=", (&unique_id) );
		//+20

		//get ranges length
		if ((length - processed) < 4){
			log_errorln_sz( "Failed to get device id list length. length=", length );
			break;
		}
		if (0 != copy_from_user( &ranges_length, buffer + processed, sizeof( unsigned int ) )){
			log_errorln( "Failed to write to pipe. Invalid user buffer" );
			processed = -EINVAL;
			break;
		}
		processed += sizeof( unsigned int );
		//+24

		ranges_buffer_size = ranges_length*sizeof( struct ioctl_range_s );

		// ranges
		if ((length - processed) < (ranges_buffer_size)){
			log_errorln_sz( "Invalid ctrl pipe next portion command. Failed to get all ranges. length=", length );
			break;
		}
		ranges = page_array_alloc( page_count_calc( ranges_buffer_size ), GFP_KERNEL );
		if (ranges == NULL){
			log_errorln( "Failed to process next portion command. Failed to allocate page array buffer" );
			processed = -ENOMEM;
			break;
		}
		if (ranges_buffer_size != page_array_user2page( buffer + processed, 0, ranges, ranges_buffer_size )){
			log_errorln( "Failed to process next portion command. Invalid user buffer from parameters." );
			processed = -EINVAL;
			break;
		}
		processed += ranges_buffer_size;
		//+40
#ifdef SNAPSTORE
		{
			result = snapstore_add_file( &unique_id, ranges, ranges_length );

			if (result != SUCCESS){
				log_errorln( "Cannot add file to snapstore" );
				result = -ENODEV;
				break;
			}
		}
#else //SNAPSTORE
		{
			snapshotdata_stretch_t* stretch = NULL;
			stretch = snapshotdata_stretch_find( &unique_id );
			if (stretch == NULL){
				log_errorln( "Cannot find stretch snapshot data" );
				result = -ENODEV;
				break;
			}

			result = snapshotdata_add_ranges( &stretch->file, &stretch->blkinfo, ranges, ranges_length );
			if (result == SUCCESS)
				stretch->halffilled = false;

#ifdef SNAPDATA_ZEROED
			{
				int res = SUCCESS;
				snapshotdata_t* snapshotdata = NULL;
				res = snapshotdata_FindByDevId( stretch->file.blk_dev_id, &snapshotdata );
				if (res != SUCCESS){
					log_traceln_dev_t( "Cannot set zero sectors for device=", stretch->file.blk_dev_id );
				}
				else
					res = zerosectors_add_ranges( &snapshotdata->zero_sectors, ranges, (size_t)ranges_length );
			}
#endif
		}
#endif //SNAPSTORE
	} while (false);
	if (ranges)
		page_array_free( ranges );

	if (result == SUCCESS)
		//log_traceln_sz( "processed=", processed );
		return processed;
	return result;
}

void ctrl_pipe_push_request( ctrl_pipe_t* pipe, unsigned int* cmd, size_t cmd_len )
{
	cmd_to_user_t* request = NULL;

	//log_traceln( "." );

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

	//log_traceln( "." );

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
	cmd[1] = (unsigned int)(filled_status & 0xFFFFffff); //lo
	cmd[2] = (unsigned int)(filled_status >> 32);

	ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_overflow( ctrl_pipe_t* pipe, unsigned int error_code, unsigned long long filled_status )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 4;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_OVERFLOW;
	cmd[1] = error_code;
	cmd[2] = (unsigned int)(filled_status & 0xFFFFffff); //lo
	cmd[3] = (unsigned int)(filled_status >> 32);

	ctrl_pipe_push_request( pipe, cmd, cmd_len );
}

void ctrl_pipe_request_terminate( ctrl_pipe_t* pipe, unsigned long long filled_status )
{
	unsigned int* cmd = NULL;
	size_t cmd_len = 3;

	log_traceln( "." );

	cmd = (unsigned int*)dbg_kmalloc( cmd_len * sizeof( unsigned int ), GFP_KERNEL );
	if (NULL == cmd){
		log_errorln( "Failed to create command acknowledge data." );
		return;
	}

	cmd[0] = (unsigned int)VEEAMSNAP_CHARCMD_TERMINATE;
	cmd[1] = (unsigned int)(filled_status & 0xFFFFffff); //lo
	cmd[2] = (unsigned int)(filled_status >> 32);

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


