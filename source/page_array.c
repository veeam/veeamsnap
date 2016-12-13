#include "stdafx.h"
#include "page_array.h"

atomic64_t page_alloc_count;
atomic64_t page_free_count;
atomic64_t page_array_alloc_count;
atomic64_t page_array_free_count;

void page_arrays_init( void )
{
	atomic64_set( &page_alloc_count, 0 );
	atomic64_set( &page_free_count, 0 );

	atomic64_set( &page_array_alloc_count, 0 );
	atomic64_set( &page_array_free_count, 0 );
}
void page_arrays_print_state( void )
{
	pr_warn( "\n" );
	pr_warn( "%s:\n", __FUNCTION__ );
	pr_warn( "pages allocated: %lld \n", (long long int)atomic64_read( &page_alloc_count ) );
	pr_warn( "pages freed: %lld \n", (long long int)atomic64_read( &page_free_count ) );
	pr_warn( "pages in use: %lld \n", (long long int)atomic64_read( &page_alloc_count )- (long long int)atomic64_read( &page_free_count ) );

	pr_warn( "arrays allocated: %lld \n", (long long int)atomic64_read( &page_array_alloc_count ) );
	pr_warn( "arrays freed: %lld \n", (long long int)atomic64_read( &page_array_free_count ) );
	pr_warn( "arrays in use: %lld \n", (long long int)atomic64_read( &page_array_alloc_count ) - (long long int)atomic64_read( &page_array_free_count ) );
}
//////////////////////////////////////////////////////////////////////////
page_array_t* page_array_alloc( size_t count, int gfp_opt )
{
	size_t inx;
	page_array_t* arr = NULL;
	while (NULL == (arr = dbg_kzalloc( sizeof( page_array_t ) + count*sizeof( page_info_t ), gfp_opt ))){
		log_errorln( "Cannot allocate buffer. Schedule." );
		schedule( );
	}
	arr->count = count;
	for (inx = 0; inx < arr->count; ++inx){
		while (NULL == (arr->pg[inx].page = alloc_page( gfp_opt ))){
			log_errorln( "Cannot allocate page. Schedule." );
			schedule( );
		}
		arr->pg[inx].addr = page_address( arr->pg[inx].page );
		atomic64_inc( &page_alloc_count );
	}

	atomic64_inc( &page_array_alloc_count );
	return arr;
}

void page_array_free( page_array_t* arr )
{
	size_t inx;
	size_t count = arr->count;
	if (arr == NULL)
		return;

	for (inx = 0; inx < count; ++inx){
		free_page( (unsigned long)(arr->pg[inx].addr) );
		atomic64_inc( &page_free_count );
	}
	dbg_kfree( arr );
	atomic64_inc( &page_array_free_count );
}

size_t page_array_copy2mem( char* dst, size_t arr_ofs, page_array_t* arr, size_t length )
{
	int page_inx = arr_ofs / PAGE_SIZE;
	size_t processed_len = 0;
	char* src;
	{//first
		size_t unordered = arr_ofs & (PAGE_SIZE - 1);
		size_t page_len = min_t( size_t, ( PAGE_SIZE - unordered ), length );

		src = arr->pg[page_inx].addr;
		memcpy( dst + processed_len, src + unordered, page_len );

		++page_inx;
		processed_len += page_len;
	}
	while ((processed_len < length) && (page_inx < arr->count))
	{
		size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

		src = arr->pg[page_inx].addr;
		memcpy( dst + processed_len, src, page_len );

		++page_inx;
		processed_len += page_len;
	}

	return processed_len;
}

size_t page_array_copy2user( char* dst_user, size_t arr_ofs, page_array_t* arr, size_t length )
{
	size_t left_data_length;
	int page_inx = arr_ofs / PAGE_SIZE;
	size_t processed_len = 0;

	{//first
		size_t unordered = arr_ofs & (PAGE_SIZE - 1);
		size_t page_len = min_t( size_t, (PAGE_SIZE - unordered), length );

		left_data_length = copy_to_user( dst_user + processed_len, arr->pg[page_inx].addr  + unordered, page_len );
		if (0 != left_data_length){
			log_errorln_sz( "left data length=", left_data_length );
			return processed_len;
		}

		++page_inx;
		processed_len += page_len;
	}
	while ((processed_len < length) && (page_inx < arr->count))
	{
		size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

		left_data_length = copy_to_user( dst_user + processed_len, arr->pg[page_inx].addr, page_len );
		if (0 != left_data_length){
			log_errorln_sz( "left data length=", left_data_length );
			break;
		}

		++page_inx;
		processed_len += page_len;
	}

	return processed_len;
}

size_t page_count_calculate( sector_t range_start_sect, sector_t range_cnt_sect )
{
	size_t page_count = range_cnt_sect / SECTORS_IN_PAGE;

	if (unlikely( range_cnt_sect & (SECTORS_IN_PAGE - 1) ))
		page_count += 1;
	return page_count;
}

void page_array_memset( page_array_t* arr, int value )
{
	size_t inx;
	for (inx = 0; inx < arr->count; ++inx){
		void* ptr = arr->pg[inx].addr;
		memset( ptr, value, PAGE_SIZE );
	}
}

void page_array_memcpy( page_array_t* dst, page_array_t* src )
{
	size_t inx;
	size_t count = min_t( size_t, dst->count, src->count );

	for (inx = 0; inx < count; ++inx){
		void* dst_ptr = dst->pg[inx].addr ;
		void* src_ptr = src->pg[inx].addr;
		memcpy( dst_ptr, src_ptr, PAGE_SIZE );
	}
}
