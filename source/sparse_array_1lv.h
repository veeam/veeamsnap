#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef sector_t index_t;
typedef u32 sparse_array_el_t;

#define GROUP_LENGTH_SHIFT 12 //по 4096 элементов.
#define GROUP_LENGTH (1 << GROUP_LENGTH_SHIFT)
#define GROUP_LENGTH_MASK (GROUP_LENGTH-1)


typedef struct sparse_array_group_s
{
	size_t cnt;
	unsigned char bitmap[GROUP_LENGTH >> 3];
	sparse_array_el_t values[GROUP_LENGTH];
}sparse_array_group_t;

typedef struct sparse_arr1lv_s
{
	index_t first;
	index_t last;
	sparse_array_group_t** groups;
	size_t group_count;

	struct rw_semaphore rw_lock;
}sparse_arr1lv_t;

int sparse_array_1lv_init( sparse_arr1lv_t* header, index_t first, index_t last );

void sparse_array_1lv_done( sparse_arr1lv_t* header );

void sparse_array_1lv_reset( sparse_arr1lv_t* header );

int sparse_array_1lv_set( sparse_arr1lv_t* header, index_t inx, sparse_array_el_t value, sparse_array_el_t* p_prev_value );

int sparse_array_1lv_get( sparse_arr1lv_t* header, index_t inx, sparse_array_el_t* p_value );

#ifdef __cplusplus
}
#endif  /* __cplusplus */
