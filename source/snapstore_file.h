#pragma once
#ifdef SNAPSTORE
#include "blk_deferred.h"

typedef struct snapstore_file_s{
	dev_t blk_dev_id;
	struct block_device*  blk_dev;

	blk_descr_file_pool_t pool;
}snapstore_file_t;

int snapstore_file_create( dev_t dev_id, snapstore_file_t** pfile );

void snapstore_file_destroy( snapstore_file_t* file );

bool snapstore_file_check_halffill( snapstore_file_t* file, sector_t empty_limit, sector_t* fill_status );

#endif //SNAPSTORE
