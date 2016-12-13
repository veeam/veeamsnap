#include "stdafx.h"

#include "sparse_bitmap.h"
#include "mem_alloc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define BLK_ST_EMPTY 0
#define BLK_ST_USE   1
#define BLK_ST_FULL  2

//////////////////////////////////////////////////////////////////////////
struct kmem_cache* g_sparse_block_cache = NULL;

int sparsebitmap_init( void )
{
#ifdef SPARSE_BLOCK_CACHEABLE
	g_sparse_block_cache = kmem_cache_create( "VeeamSparseBlockCache", sizeof( blocks_array_t ), 0, 0, NULL );
	if (g_sparse_block_cache == NULL){
		log_traceln( "Cannot create kmem_cache. Name=VeeamSparseBlockCache" );
		return -ENOMEM;
	}
#endif
	return SUCCESS;
}

void  sparsebitmap_done( void )
{
#ifdef SPARSE_BLOCK_CACHEABLE
	if (g_sparse_block_cache != NULL){
		kmem_cache_destroy( g_sparse_block_cache );
		g_sparse_block_cache = NULL;
	}
#endif
}

static inline blocks_array_t* __sparse_block_array_new( int init_value )
{
	blocks_array_t* blocks_array = NULL;
#ifdef SPARSE_BLOCK_CACHEABLE
	while (NULL == (blocks_array = kmem_cache_alloc( g_sparse_block_cache, GFP_NOIO ))){
#else
	while (NULL == (blocks_array = dbg_kmalloc( sizeof( blocks_array_t ), GFP_NOIO ))){
#endif
		log_errorln( "Cannot allocate memory NOIO. Schedule." );
		schedule( );
	}
	memset( blocks_array, init_value, sizeof( blocks_array_t ) );
	return blocks_array;
}

static inline void __sparse_block_array_free( blocks_array_t* blocks_array )
{
#ifdef SPARSE_BLOCK_CACHEABLE
	kmem_cache_free( g_sparse_block_cache, blocks_array );
#else
	dbg_kfree( blocks_array );
#endif
}

//////////////////////////////////////////////////////////////////////////
void __sparse_block_init( sparse_block_t* block, char level, void* block_state )
{
	block->level = level;

	if (block_state == BLOCK_EMPTY){
		block->fill_count = 0;
		block->cnt_full = 0;
	}
	else{
		block->fill_count = SPARSE_BITMAP_BLOCK_SIZE;
		block->cnt_full = SPARSE_BITMAP_BLOCK_SIZE;
	}

	block->blocks_array = block_state;
}
sparse_block_t* __sparse_block_create( char level, void* block_state )
{
	sparse_block_t* block;
	while (NULL == (block = dbg_kmalloc( sizeof( sparse_block_t ), GFP_NOIO ))){
		log_errorln( "Cannot allocate memory NOIO. Schedule." );
		schedule( );
	}

	if (block != NULL)
		__sparse_block_init( block, level, block_state );
	return block;
}
//////////////////////////////////////////////////////////////////////////
void __sparse_block_destroy( sparse_block_t* block )
{
	dbg_kfree( block );
}
//////////////////////////////////////////////////////////////////////////
void __sparse_block_free( sparse_block_t* block )
{
	if (block->level == 0){
		block->bit_block = 0;
		block->fill_count = 0;
	}
	else{
		if ((block->blocks_array != BLOCK_EMPTY) && (block->blocks_array != BLOCK_FULL)){
			int inx;
			for (inx = 0; inx < SPARSE_BITMAP_BLOCK_SIZE; inx++){

				if ((block->blocks_array->blk[inx] != BLOCK_FULL) && (block->blocks_array->blk[inx] != BLOCK_EMPTY)){
					__sparse_block_free( block->blocks_array->blk[inx] );

					__sparse_block_destroy( block->blocks_array->blk[inx] );
					block->blocks_array->blk[inx] = NULL;
				}
			}

			__sparse_block_array_free( block->blocks_array );
			block->blocks_array = NULL;

			block->fill_count = 0;
			block->cnt_full = 0;
		}
	}
}
//////////////////////////////////////////////////////////////////////////
int __sparse_block_clear( sparse_block_t* block, stream_size_t index, char* p_blk_st )
{
	char blk_st = BLK_ST_USE;
	int res = SUCCESS;

	if (block->level == 0){
		size_t inx = (size_t)(index  & SPARSE_BITMAP_BLOCK_SIZE_MASK);
		size_t bit_mask = ((size_t)(1) << inx);

		if (block->bit_block & bit_mask){//is set
			block->bit_block &= ~bit_mask;
			--block->fill_count;
		}
		else
			res = -EALREADY;

		if (block->fill_count == 0)
			blk_st = BLK_ST_EMPTY;

		*p_blk_st = blk_st;
		return res;
	}

	do{
		size_t inx = (size_t)(index >> (SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level)) & SPARSE_BITMAP_BLOCK_SIZE_MASK;

		if (block->blocks_array == BLOCK_EMPTY){
			blk_st = BLK_ST_EMPTY;
			break;
		}

		if (block->blocks_array == BLOCK_FULL)
			block->blocks_array = __sparse_block_array_new( 0xFF );//all blocks is full

		if (block->blocks_array->blk[inx] == BLOCK_EMPTY)
			break; //already empty

		if (block->blocks_array->blk[inx] == BLOCK_FULL){
			block->blocks_array->blk[inx] = __sparse_block_create( block->level - 1, BLOCK_FULL );
			if (block->blocks_array->blk[inx] == NULL){
				res = -ENOMEM;
				break;
			}
			--block->cnt_full;
		}

		{
			char sub_blk_state;
			res = __sparse_block_clear( block->blocks_array->blk[inx], index, &sub_blk_state );
			if (res != SUCCESS)
				break;

			if (sub_blk_state == BLK_ST_EMPTY){
				__sparse_block_destroy( block->blocks_array->blk[inx] );
				block->blocks_array->blk[inx] = BLOCK_EMPTY;
				--block->fill_count;

				if (block->fill_count == 0){
					blk_st = BLK_ST_EMPTY;

					__sparse_block_array_free( block->blocks_array );
					block->blocks_array = BLOCK_EMPTY;
				}
			}
		}

	} while (false);

	*p_blk_st = blk_st;
	return res;
}
//////////////////////////////////////////////////////////////////////////
int __sparse_block_set( sparse_block_t* block, stream_size_t index, char* p_blk_st )
{
	char blk_st = BLK_ST_USE;
	int res = SUCCESS;

	if (block->level == 0){
		size_t inx = (size_t)(index  & SPARSE_BITMAP_BLOCK_SIZE_MASK);
		size_t bit_mask = ((size_t)(1) << inx);

		if ((block->bit_block & bit_mask) == 0){//is non set
			block->bit_block |= bit_mask;
			++block->fill_count;
		}
		else
			res = -EALREADY;


		if (block->fill_count == SPARSE_BITMAP_BLOCK_SIZE)
			blk_st = BLK_ST_FULL;

		*p_blk_st = blk_st;
		return res;
	}

	do{
		size_t inx = (size_t)(index >> (stream_size_t)(SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level)) & SPARSE_BITMAP_BLOCK_SIZE_MASK;

		if (block->blocks_array == BLOCK_FULL){
			res = -EALREADY;
			break;
		}

		if (block->blocks_array == BLOCK_EMPTY)
			block->blocks_array = __sparse_block_array_new( 0x00 ); //all blocks is empty

		if (block->blocks_array->blk[inx] == BLOCK_FULL){
			res = -EALREADY;
			break; //already full
		}

		if (block->blocks_array->blk[inx] == BLOCK_EMPTY){
			block->blocks_array->blk[inx] = __sparse_block_create( block->level - 1, BLOCK_EMPTY );
			if (block->blocks_array->blk[inx] == NULL){
				res = -ENOMEM;
				break;
			}
			++block->fill_count;
		}

		{
			char sub_blk_st;
			res = __sparse_block_set( block->blocks_array->blk[inx], index, &sub_blk_st );
			if (res != SUCCESS)
				break;

			if (sub_blk_st == BLK_ST_FULL){
				//log_errorln_llx( "block full. index=", index );

				__sparse_block_destroy( block->blocks_array->blk[inx] );
				block->blocks_array->blk[inx] = BLOCK_FULL;
				++block->cnt_full;

				if (block->cnt_full == SPARSE_BITMAP_BLOCK_SIZE){
					//log_errorln_llx( "block array full. index=", index );

					blk_st = BLK_ST_FULL;

					__sparse_block_array_free( block->blocks_array );
					block->blocks_array = BLOCK_FULL;
				}
			}
		}

	} while (false);

	*p_blk_st = blk_st;

	return res;
}
//////////////////////////////////////////////////////////////////////////
bool __sparse_block_get( sparse_block_t* block, stream_size_t index )
{
	bool result;
	if (block->level == 0){
		size_t inx = (size_t)(index  & SPARSE_BITMAP_BLOCK_SIZE_MASK);
		size_t bit_mask = ((size_t)(1) << inx);

		result = ((block->bit_block & bit_mask) != 0);
	}
	else{
		do{
			size_t inx = (size_t)(index >> (SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level)) & SPARSE_BITMAP_BLOCK_SIZE_MASK;

			if (block->blocks_array == BLOCK_FULL){
				result = true;
				break;
			}
			if (block->blocks_array == BLOCK_EMPTY){
				result = false;
				break;
			}

			if (block->blocks_array->blk[inx] == BLOCK_FULL){
				result = true;
				break;
			}
			if (block->blocks_array->blk[inx] == BLOCK_EMPTY){
				result = false;
				break;
			}

			result = __sparse_block_get( block->blocks_array->blk[inx], index );
		} while (false);
	}
	return result;
}
//////////////////////////////////////////////////////////////////////////
char __calc_level( stream_size_t ull )
{
	char level = 0;
	while (ull > SPARSE_BITMAP_BLOCK_SIZE){
		ull = ull >> SPARSE_BITMAP_BLOCK_SIZE_DEGREE;
		level++;
	}
	return level;
}

//////////////////////////////////////////////////////////////////////////
void sparsebitmap_create( sparse_bitmap_t* bitmap, stream_size_t min_index, stream_size_t length )
{
	char level = __calc_level( length );
	bitmap->start_index = min_index;
	bitmap->length = length;

	log_traceln_llx( "start_index=", bitmap->start_index );
	log_traceln_llx( "length=", bitmap->length );
	log_traceln_d( "levels=", level );

	__sparse_block_init( &(bitmap->sparse_block), level, BLOCK_EMPTY );
}
//////////////////////////////////////////////////////////////////////////
void sparsebitmap_destroy( sparse_bitmap_t* bitmap )
{
	sparsebitmap_Clean( bitmap );
}
//////////////////////////////////////////////////////////////////////////

int sparsebitmap_Set( sparse_bitmap_t* bitmap, stream_size_t index, bool state )
{
	int res;
	char blk_st;

	if ((index < bitmap->start_index) || (index >= (bitmap->start_index + bitmap->length))){
		log_errorln_llx( "Out of range. index=", index );
		return -EINVAL;
	}
	index = index - bitmap->start_index;

	if (state)
		res = __sparse_block_set( &bitmap->sparse_block, index, &blk_st );
	else
		res = __sparse_block_clear( &bitmap->sparse_block, index, &blk_st );

	return res;
}
//////////////////////////////////////////////////////////////////////////
int sparsebitmap_Get( sparse_bitmap_t* bitmap, stream_size_t index, bool* p_state )
{
	if ((index < bitmap->start_index) || (index >= (bitmap->start_index + bitmap->length)))
		return -EINVAL;
	index = index - bitmap->start_index;

	*p_state = __sparse_block_get( &bitmap->sparse_block, index );
	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////
void sparsebitmap_Clean( sparse_bitmap_t* bitmap )
{
	__sparse_block_free( &bitmap->sparse_block );
	bitmap->length = 0;
	bitmap->start_index = 0;
}
//////////////////////////////////////////////////////////////////////////
int sparsebitmap_SetRange( sparse_bitmap_t* bitmap, range_t* rg, bool state )
{
	char blk_st;
	stream_size_t index;

	if ((rg->ofs < bitmap->start_index) || ((rg->ofs+rg->cnt) >= (bitmap->start_index + bitmap->length))){
		log_errorln( "Out of range");
		log_errorln_sect( "  ofs= ", rg->ofs );
		log_errorln_sect( "  cnt= ", rg->cnt );
		return -EINVAL;
	}

	for (index = rg->ofs - bitmap->start_index; index < (rg->ofs + rg->cnt - bitmap->start_index); ++index){
		int res;
		if (state)
			res = __sparse_block_set( &bitmap->sparse_block, index, &blk_st );
		else
			res = __sparse_block_clear( &bitmap->sparse_block, index, &blk_st );

		if ((res != SUCCESS) && (res != -EALREADY)){
			log_errorln_lld( "Failed to set bit #", index );
			return res;
		}
	}

	return SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
int sparsebitmap_GetFirstRange( sparse_bitmap_t* bitmap, stream_size_t min_index, range_t* p_rg )
{
	stream_size_t index = min_index;
	range_t rg;

	if ((min_index < bitmap->start_index) || (min_index >= (bitmap->start_index + bitmap->length)))
		return -EINVAL;

	rg.cnt = 0;
	rg.ofs = 0;
	do{
		if (__sparse_block_get( &bitmap->sparse_block, index )){
			if (rg.cnt == 0)
				rg.ofs = index;
			++rg.cnt;
		}
		else{
			if (rg.cnt != 0)
				break; // found end of range
		}

		++index;
	} while (index < bitmap->length);

	if (rg.cnt == 0)
		return -ENODATA;

	p_rg->ofs = rg.ofs + bitmap->start_index;
	p_rg->cnt = rg.cnt;

	return SUCCESS;
}
//////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif  /* __cplusplus */