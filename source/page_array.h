#pragma once

#ifndef PAGE_ARRAY_H
#define PAGE_ARRAY_H

#include "sector.h"

typedef struct page_info_s{
	struct page* page;
	void* addr;
}page_info_t;

typedef struct page_array_s
{
	size_t count;
	page_info_t pg[0];
}page_array_t;

void page_arrays_init( void );
void page_arrays_print_state( void );

page_array_t* page_array_alloc( size_t count, int gfp_opt );
void page_array_free( page_array_t* arr );

size_t page_array_copy2mem( char* dst_buffer, size_t arr_ofs, page_array_t* arr, size_t length );
size_t page_array_copy2user( char* dst_user_buffer, size_t arr_ofs, page_array_t* arr, size_t length );

size_t page_count_calculate( sector_t range_start_sect, sector_t range_cnt_sect );

static inline char* page_get_sector( page_array_t* arr, sector_t arr_ofs )
{
	size_t pg_inx = arr_ofs >> (PAGE_SHIFT - SECTOR512_SHIFT);
	size_t pg_ofs = sector_to_size( arr_ofs & ((1 << (PAGE_SHIFT - SECTOR512_SHIFT)) - 1) );

	return (arr->pg[pg_inx].addr + pg_ofs);
}

void page_array_memset( page_array_t* arr, int value );
void page_array_memcpy( page_array_t* dst, page_array_t* src );

static inline byte_t page_array_byte_get( page_array_t* arr, size_t inx )
{
	size_t page_inx = inx >> PAGE_SHIFT;
	size_t byte_pos = inx & (PAGE_SIZE - 1);
	byte_t* ptr = arr->pg[page_inx].addr;
	return ptr[byte_pos];
};

static inline void page_array_byte_set( page_array_t* arr, size_t inx, byte_t value )
{
	size_t page_inx = inx >> PAGE_SHIFT;
	size_t byte_pos = inx & (PAGE_SIZE - 1);
	byte_t* ptr = arr->pg[page_inx].addr;
	ptr[byte_pos] = value;
};

static inline bool page_array_bit_get( page_array_t* arr, size_t inx )
{
	byte_t v;
	size_t byte_inx = (inx / BITS_PER_BYTE);
	size_t bit_inx = (inx & (BITS_PER_BYTE - 1));
	v = page_array_byte_get( arr, byte_inx );
	return v & (1 << bit_inx);
};

static inline void page_array_bit_set( page_array_t* arr, size_t inx, bool value )
{
	byte_t v;
	size_t byte_inx = (inx / BITS_PER_BYTE);
	size_t bit_inx = (inx & (BITS_PER_BYTE - 1));

	size_t page_inx = byte_inx >> PAGE_SHIFT;
	size_t byte_pos = byte_inx & (PAGE_SIZE - 1);
	byte_t* ptr = arr->pg[page_inx].addr;

	v = ptr[byte_pos];
	if (value){
		v |= (1 << bit_inx);
	}
	else{
		v &= ~(1 << bit_inx);
	}
	ptr[byte_pos] = v;
};

#endif //PAGE_ARRAY_H