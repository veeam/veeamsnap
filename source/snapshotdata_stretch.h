#pragma once

#ifndef SNAPSTORE

#include "snapshotdata_shared.h"
#include "snapshotdata_blkinfo.h"
#include "snapshotdata_file.h"
#include "ctrl_pipe.h"

typedef struct snapshotdata_stretch_s{
	snapshotdata_shared_t shared;

	snapshotdata_blkinfo_t blkinfo;

	snapshotdata_file_t file;

	ctrl_pipe_t* ctrl_pipe;

	sector_t empty_limit;

	volatile bool halffilled;
	volatile bool overflowed;
}snapshotdata_stretch_t;


int snapshotdata_stretch_Init( void );
int snapshotdata_stretch_Done( void );

void snapshotdata_stretch_free( snapshotdata_stretch_t* stretch_disk );
snapshotdata_stretch_t* snapshotdata_stretch_create( uuid_t* id, dev_t dev_id );

snapshotdata_stretch_t* snapshotdata_stretch_find( uuid_t* id );

//void snapshotdata_stretch_halffill( snapshotdata_stretch_t* stretch_disk, ssize_t fill_status );
void snapshotdata_stretch_check_halffill( snapshotdata_stretch_t* stretch, snapshotdata_blkinfo_t* blkinfo );
void snapshotdata_stretch_overflow( snapshotdata_stretch_t* stretch_disk, unsigned int error_code, snapshotdata_blkinfo_t* blkinfo );
void snapshotdata_stretch_terminate( snapshotdata_stretch_t* stretch_disk );


#endif //SNAPSTORE
