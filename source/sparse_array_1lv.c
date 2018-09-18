#include "stdafx.h"
#include "sparse_array_1lv.h"


int sparse_array_1lv_init( sparse_arr1lv_t* header, sparse_array_index_t first, sparse_array_index_t last )
{
	init_rwsem( &header->rw_lock );

	header->first = first;
	header->last = last;

	header->group_count = (size_t)((last + 1 - first) >> GROUP_LENGTH_SHIFT);
	if ((last + 1 - first) & GROUP_LENGTH_MASK)
		++(header->group_count);

	header->groups = dbg_vzalloc( header->group_count * sizeof( sparse_array_group_t* ));
	if (NULL == header->groups){
		sparse_array_1lv_done( header );
		return -ENOMEM;
	}

	return SUCCESS;
}

void sparse_array_1lv_reset( sparse_arr1lv_t* header )
{
	size_t gr_idx;
	if (header->groups != NULL){
		for (gr_idx = 0; gr_idx < header->group_count; ++gr_idx){
			if (header->groups[gr_idx] != NULL){
				dbg_kfree( header->groups[gr_idx] );
				header->groups[gr_idx] = NULL;
			}
		}
	}
}

void sparse_array_1lv_done( sparse_arr1lv_t* header )
{
	if (header->groups != NULL){

		sparse_array_1lv_reset( header );

		dbg_vfree( header->groups, header->group_count * sizeof( sparse_array_group_t* ) );
		header->groups = NULL;
	}
}

int sparse_array_1lv_set( sparse_arr1lv_t* header, sparse_array_index_t inx, sparse_array_el_t value, sparse_array_el_t* p_prev_value )
{
	int res = SUCCESS;
	size_t gr_idx;
	size_t val_idx;
	sparse_array_group_t* pgr = NULL;
	unsigned char bits;

	down_write( &header->rw_lock );
	do{
		if (!((header->first <= inx) && (inx <= header->last))){
			res = -EINVAL;
			break;
		}

		gr_idx = (size_t)((inx - header->first) >> GROUP_LENGTH_SHIFT);
		if (header->groups[gr_idx] == NULL){

			while (NULL == (header->groups[gr_idx] = dbg_kzalloc( sizeof( sparse_array_group_t ), GFP_NOIO ))){
				log_errorln( "Cannot allocate memory NOIO. Schedule." );
				schedule( );
			}
			if (header->groups[gr_idx] == NULL){
				res = -ENOMEM;
				break;
			}
		}
		val_idx = (size_t)((inx - header->first) & GROUP_LENGTH_MASK);

		pgr = header->groups[gr_idx];

		bits = (1 << (val_idx & 0x7));
		if (pgr->bitmap[val_idx >> 3] & bits){
			if (NULL != p_prev_value)
				*p_prev_value = pgr->values[val_idx];
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

static int _get_down( sparse_arr1lv_t* header, sparse_array_index_t inx, sparse_array_el_t* p_value, bool is_down )
{
	int res = SUCCESS;
	size_t gr_idx;
	size_t val_idx;
	sparse_array_group_t* pgr = NULL;
	unsigned char bits;

	down_read( &header->rw_lock );
	do{

		if ((inx < header->first) || (header->last < inx)){
			res = -EINVAL;
			break;
		}

		gr_idx = (size_t)((inx - header->first) >> GROUP_LENGTH_SHIFT);
		if (header->groups[gr_idx] == NULL){
			res = -ENODATA;
			break;
		}
		val_idx = (size_t)((inx - header->first) & GROUP_LENGTH_MASK);

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

int sparse_array_1lv_get( sparse_arr1lv_t* header, sparse_array_index_t inx, sparse_array_el_t* p_value )
{
	return _get_down( header, inx, p_value, false );
}

int sparse_array_1lv_get_down( sparse_arr1lv_t* header, sparse_array_index_t inx, sparse_array_el_t* p_value )
{
	return _get_down( header, inx, p_value, true );
}

