#include "stdafx.h"
#include "sector.h"
#include "page_array.h"
#include "zerosectors.h"

#ifdef SNAPDATA_ZEROED

int zerosectors_add_ranges( rangevector_t* zero_sectors, page_array_t* ranges, size_t ranges_cnt )
{
	if ((ranges == NULL) || (ranges_cnt == 0))
		return -EINVAL;

	if (get_zerosnapdata( )){
		unsigned int inx = 0;

		//log_traceln_d( "Zero sectors set. ranges: ", ranges_length );
		for (inx = 0; inx < ranges_cnt; ++inx){
			int res = SUCCESS;
			range_t range;
			struct ioctl_range_s* ioctl_range = (struct ioctl_range_s*)page_get_element( ranges, inx, sizeof( struct ioctl_range_s ) );

			range.ofs = sector_from_streamsize( ioctl_range->left );
			range.cnt = sector_from_streamsize( ioctl_range->right ) - range.ofs;

			//log_traceln_range( "range:", range );
			res = rangevector_add( zero_sectors, &range );
			if (res != SUCCESS){
				log_errorln( "Cannot add range to zero sectors" );
				return res;
			}
		}
		rangevector_sort( zero_sectors );
	}
	return SUCCESS;
}

#ifndef SNAPSTORE

int zerosectors_add_file( rangevector_t* zero_sectors, dev_t dev_id, snapshotdata_file_t* file )
{
	int res = SUCCESS;
	if (get_zerosnapdata( ) && (file != NULL)){
		if (dev_id == file->blk_dev_id){// snapshot data on same partition
			range_t* range;
			//size_t range_inx = 0;
			//rangevector_t* snaprange = file->dataranges;

			log_traceln_dev_t( "Zeroing ranges set for device ", dev_id );
			log_traceln_sect( "Zeroing range sectors ", rangevector_length( &file->dataranges ) );
			RANGEVECTOR_FOREACH_BEGIN( &file->dataranges, range )
			{
				res = rangevector_add( zero_sectors, range );
				if (res != SUCCESS)
					break;
				//log_traceln_range( "Zeroing range:", (*range) );
			}
			RANGEVECTOR_FOREACH_END( );

			if (res == SUCCESS)
				rangevector_sort( zero_sectors );
			else
				log_errorln_d( "Failed to set zero sectors. errno=", res );
		}
		else
			log_traceln_dev_t( "Haven`t zeroing ranges for device", dev_id );
	}
	else
		log_traceln_dev_t( "Do not zeroing ranges for device ", dev_id );
	return res;
}

#endif // SNAPSTORE

#endif //SNAPDATA_ZEROED