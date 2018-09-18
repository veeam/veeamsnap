#include "stdafx.h"
#include "blk_descr_array.h"

//#define _TRACE
int blk_descr_array_init( blk_descr_array_t* header, blk_descr_array_index_t first, blk_descr_array_index_t last )
{
	init_rwsem( &header->rw_lock );

	header->first = first;
	header->last = last;

	header->group_count = (size_t)((last + 1 - first) >> BLK_DESCR_GROUP_LENGTH_SHIFT);
	if ((last + 1 - first) & BLK_DESCR_GROUP_LENGTH_MASK)
		++(header->group_count);

	header->groups = dbg_vzalloc( header->group_count * sizeof( blk_descr_array_group_t* ) );
	if (NULL == header->groups){
		blk_descr_array_done( header );
		return -ENOMEM;
	}

	return SUCCESS;
}

void blk_descr_array_done( blk_descr_array_t* header )
{
	if (header->groups != NULL){

		blk_descr_array_reset( header );

		dbg_vfree( header->groups, header->group_count * sizeof( blk_descr_array_group_t* ) );
		header->groups = NULL;
	}
}

void blk_descr_array_reset( blk_descr_array_t* header )
{
	size_t gr_idx;
	if (header->groups != NULL){
		for (gr_idx = 0; gr_idx < header->group_count; ++gr_idx){
			if (header->groups[gr_idx] != NULL){
				dbg_kfree( header->groups[gr_idx] );
				header->groups[gr_idx] = NULL;

#ifdef _TRACE
				log_warnln( "--" );
#endif
			}
		}
	}
}

int blk_descr_array_set( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t value )
{
	int res = SUCCESS;
	size_t gr_idx;
	size_t val_idx;
	blk_descr_array_group_t* pgr = NULL;
	unsigned char bits;

	down_write( &header->rw_lock );
	do{
		if (!((header->first <= inx) && (inx <= header->last))){
			res = -EINVAL;
			break;
		}

		gr_idx = (size_t)((inx - header->first) >> BLK_DESCR_GROUP_LENGTH_SHIFT);
		if (header->groups[gr_idx] == NULL){

			header->groups[gr_idx] = dbg_kzalloc( sizeof( blk_descr_array_group_t ), GFP_NOIO );
			if (header->groups[gr_idx] == NULL){
				res = -ENOMEM;
				break;
			}
#ifdef _TRACE
			log_warnln( "++" );
#endif
		}
		val_idx = (size_t)((inx - header->first) & BLK_DESCR_GROUP_LENGTH_MASK);

		pgr = header->groups[gr_idx];

		bits = (1 << (val_idx & 0x7));
		if (pgr->bitmap[val_idx >> 3] & bits){
			// rewrite
		}
		else{
			pgr->bitmap[val_idx >> 3] |= bits;
			++pgr->cnt;
		}
		pgr->values[val_idx] = value;


	} while (false);
	up_write( &header->rw_lock );

	return res;
}

static int _get_down( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t* p_value, bool is_down )
{
	int res = SUCCESS;
	size_t gr_idx;
	size_t val_idx;
	blk_descr_array_group_t* pgr = NULL;
	unsigned char bits;

	down_read( &header->rw_lock );
	do{

		if ((inx < header->first) || (header->last < inx)){
			res = -EINVAL;
			break;
		}

		gr_idx = (size_t)((inx - header->first) >> BLK_DESCR_GROUP_LENGTH_SHIFT);
		if (header->groups[gr_idx] == NULL){
			res = -ENODATA;
			break;
		}
		val_idx = (size_t)((inx - header->first) & BLK_DESCR_GROUP_LENGTH_MASK);

		pgr = header->groups[gr_idx];
		bits = (1 << (val_idx & 0x7));
		if (pgr->bitmap[val_idx >> 3] & bits){
			*p_value = pgr->values[val_idx];

			if (is_down){
				pgr->values[val_idx] = 0;
				pgr->bitmap[val_idx >> 3] &= ~bits;
				--pgr->cnt;
				if (pgr->cnt == 0){
					dbg_kfree( pgr );
					pgr = NULL;

#ifdef _TRACE
					log_warnln( "--" );
#endif
				}
			}
		}
		else{
			res = -ENODATA;
			break;
		}
	} while (false);
	up_read( &header->rw_lock );

	return res;
}

int blk_descr_array_get( blk_descr_array_t* header, blk_descr_array_index_t inx, blk_descr_array_el_t* p_value )
{
	return _get_down( header, inx, p_value, false );
}

