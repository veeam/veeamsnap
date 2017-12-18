#include "stdafx.h"
#ifdef SNAPSTORE

#include "snapstore_file.h"
#include "blk_util.h"

int snapstore_file_create( dev_t dev_id, snapstore_file_t** pfile )
{
	int res = SUCCESS;
	snapstore_file_t* file;

	log_traceln( "." );

	file = dbg_kzalloc( sizeof( snapstore_file_t ), GFP_KERNEL );
	if (file == NULL)
		return -ENOMEM;

	res = blk_dev_open( dev_id, &file->blk_dev );
	if (res != SUCCESS){
		dbg_kfree( file );
		log_errorln_d( "Failed to open snapshot device. res=", res );
		return res;
	}

	file->blk_dev_id = dev_id;
	blk_descr_file_pool_init( &file->pool );

	*pfile = file;
	return res;
}

void snapstore_file_destroy( snapstore_file_t* file )
{
	if (file){
		log_traceln( "." );

		blk_descr_file_pool_done( &file->pool );

		if (file->blk_dev != NULL){
			blk_dev_close( file->blk_dev );
			file->blk_dev = NULL;
		}

		dbg_kfree(file);
	}
}

bool snapstore_file_check_halffill( snapstore_file_t* file, sector_t empty_limit, sector_t* fill_status )
{
	size_t empty_blocks = (file->pool.total_cnt - file->pool.take_cnt);

	*fill_status = (sector_t)(file->pool.take_cnt) << SNAPSTORE_BLK_SHIFT;

	return (empty_blocks < (size_t)(empty_limit >> SNAPSTORE_BLK_SHIFT));
}

#endif //SNAPSTORE
