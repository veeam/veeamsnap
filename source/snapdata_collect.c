#include "stdafx.h"
#include "snapdata_collect.h"
#include "blk_util.h"


static container_sl_t SnapdataCollectors;


int _collector_init( snapdata_collector_t* collector, dev_t dev_id, void* MagicUserBuff, size_t MagicLength );
void _collector_free( snapdata_collector_t* collector );


int snapdata_collect_Init( void )
{
	container_sl_init( &SnapdataCollectors, sizeof( snapdata_collector_t ));
	return SUCCESS;
}


int snapdata_collect_Done( void )
{
	int res;
	content_sl_t* content = NULL;

	while (NULL != (content = container_sl_get_first( &SnapdataCollectors ))){
		_collector_free( (snapdata_collector_t*)content );
		content_sl_free( (content_sl_t*)content );
		content = NULL;
	}

	res = container_sl_done( &SnapdataCollectors );
	if (res != SUCCESS){
		log_errorln_s( "Failed to free container =", "vsnap_SnapdataCollectors" );
	}
	return res;
}


int _collector_init( snapdata_collector_t* collector, dev_t dev_id, void* MagicUserBuff, size_t MagicLength )
{
	int res = SUCCESS;

	collector->fail_code = SUCCESS;

	collector->dev_id = dev_id;

	res = blk_dev_open( collector->dev_id, &collector->device );
	if (res != SUCCESS){
		log_errorln_d( "Caanot open device. rresult=", res );
		return res;
	}

	collector->magic_size = MagicLength;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,13,0)
	collector->magic_buff = dbg_kmalloc( collector->magic_size, GFP_KERNEL | __GFP_REPEAT );
#else
	collector->magic_buff = dbg_kmalloc( collector->magic_size, GFP_KERNEL | __GFP_RETRY_MAYFAIL );
#endif
	if (collector->magic_buff == NULL){
		log_errorln( "Failed to reference tracker_queue " );
		return -ENOMEM;
	}
	if (0 != copy_from_user( collector->magic_buff, MagicUserBuff, collector->magic_size )){
		log_errorln( "Invalid user buffer" );
		return -ENODATA;
	}
#ifdef SNAPDATA_SPARSE_CHANGES
	sparsebitmap_create( &collector->changes_sparse, 0, blk_dev_get_capacity( collector->device ) );
#else
	{
		stream_size_t bitmap_size = blk_dev_get_capacity( collector->device ) / BITS_PER_BYTE;
		size_t page_count = (size_t)(bitmap_size >> PAGE_SHIFT);
		if ((bitmap_size & (PAGE_SIZE - 1)) != 0)
			++page_count;

		log_traceln_lld( "Tracking snapshotdata bitmap size=", bitmap_size );

		collector->start_index = 0;
		collector->length = blk_dev_get_capacity( collector->device );

		collector->changes = page_array_alloc( page_count, GFP_KERNEL );
		if (collector->changes == NULL){
			return -ENOMEM;
		}
		page_array_memset( collector->changes, 0 );
	}
#endif
	{
		struct super_block* sb = NULL;

		res = blk_freeze_bdev( collector->dev_id, collector->device, &sb);
		if (res != SUCCESS){
			return res;
		}

		res = tracker_queue_Ref( bdev_get_queue( collector->device ), &collector->tracker_queue );
		if (res != SUCCESS){
			log_errorln( "Failed to reference tracker_queue " );
			return res;
		}

		if (sb != NULL){
			blk_thaw_bdev( collector->dev_id, collector->device, sb );
			sb = NULL;
		}
	}

	return res;
}


void _collector_stop( snapdata_collector_t* collector )
{
	if (collector->tracker_queue != NULL){
		tracker_queue_Unref( collector->tracker_queue );
		collector->tracker_queue = NULL;
	}
}


void _collector_free( snapdata_collector_t* collector )
{
	_collector_stop( collector );
#ifdef SNAPDATA_SPARSE_CHANGES
	sparsebitmap_destroy( &collector->changes_sparse );
#else
	if (collector->changes != NULL){
		page_array_free( collector->changes );
	}
#endif
	if (collector->magic_buff != NULL){
		dbg_kfree( collector->magic_buff );
		collector->magic_buff = NULL;
	}

	if (collector->device != NULL){
		blk_dev_close( collector->device );
		collector->device = NULL;
	}
}


int snapdata_collect_LocationStart( dev_t dev_id, void* MagicUserBuff, size_t MagicLength )
{
	snapdata_collector_t* collector = NULL;
	int res = -ENOTSUPP;

	log_traceln_dev_t( "Start collecting snapshot data location on device ", dev_id );

	collector = (snapdata_collector_t*)content_sl_new( &SnapdataCollectors );
	if (NULL == collector){
		log_errorln( "Cannot allocate memory for snapdata_collect_t structure." );
		return  -ENOMEM;
	}

	res = _collector_init( collector, dev_id, MagicUserBuff, MagicLength );
	if (res == SUCCESS){
		container_sl_push_back( &SnapdataCollectors, &collector->content );
	}else{
		_collector_free( collector );

		content_sl_free( &collector->content );
		collector = NULL;
	}

	return res;
}


int snapdata_collect_LocationGet( dev_t dev_id, rangelist_t* rangelist, size_t* ranges_count )
{
	size_t count = 0;
	sector_t ranges_length = 0;
	snapdata_collector_t* collector = NULL;
	int res;

	log_traceln_dev_t( "Get snapshot data location on device ", dev_id );
	res = snapdata_collect_Get( dev_id, &collector );
	if (res != SUCCESS){
		log_errorln_dev_t( "Collector not found for device=", dev_id );
		return res;
	}

	_collector_stop( collector );

	if (collector->fail_code != SUCCESS){
		log_errorln_d( "Collecting fail. err code=", 0-collector->fail_code );
		return collector->fail_code;
	}
#ifdef SNAPDATA_SPARSE_CHANGES
	{
		sector_t first_index = collector->changes_sparse.start_index;
		sparsebitmap_convert2rangelist( &collector->changes_sparse, rangelist, first_index );

		{//calculate and show information about ranges
			range_t* rg;
			RANGELIST_FOREACH_BEGIN( (*rangelist), rg )
			{
				ranges_length += rg->cnt;
				++count;
			}
			RANGELIST_FOREACH_END( );
			log_traceln_sz( "range_count=", count );
			log_traceln_sect( "ranges_length=", ranges_length );
		}
	}
#else
	{
		range_t rg = { 0 };
		sector_t index = 0;

		while (index < collector->length){
			bool bit;
			res = page_array_bit_get( collector->changes, index, &bit );
			if (res != SUCCESS)
				break;

			if ( bit ){
				if (rg.cnt == 0){
					rg.ofs = collector->start_index + index;
				}
				++rg.cnt;
			}
			else{
				if (rg.cnt == 0){
					// nothing
				}
				else{
					pr_warn( "    %s: #%ld ofs=%llx cnt=%llx\n", MODULE_NAME, count, (unsigned long long)rg.ofs, (unsigned long long)rg.cnt );

					rangelist_add( rangelist, &rg );
					ranges_length += rg.cnt;
					++count;

					rg.cnt = 0;
				}
			}
			++index;
		}

		if ((res == SUCCESS) && (rg.cnt != 0)){
			pr_warn( "    %s: #%ld ofs=%llx cnt=%llx\n", MODULE_NAME, count, (unsigned long long)rg.ofs, (unsigned long long)rg.cnt );

			rangelist_add( rangelist, &rg );
			ranges_length += rg.cnt;
			++count;

			rg.cnt = 0;

			log_traceln_sz( "range_count=", count );
			log_traceln_sect( "ranges_length=", ranges_length );
		}
	}
#endif

	if (res == SUCCESS){
		log_traceln_llx( "processed_size=", collector->collected_size );
		*ranges_count = count;
	}
	return res;
}


int snapdata_collect_LocationComplete( dev_t dev_id )
{
	snapdata_collector_t* collector = NULL;
	int res;

	log_traceln_dev_t( "Get snapshot data location on device ", dev_id );
	res = snapdata_collect_Get( dev_id, &collector );
	if (res != SUCCESS){
		log_errorln_dev_t( "Collector not found for device=", dev_id );
		return res;
	}

	_collector_free( collector );
	container_sl_free( &collector->content );

	return res;
}


int snapdata_collect_Get( dev_t dev_id, snapdata_collector_t** p_collector )
{
	int res = -ENODATA;
	content_sl_t* content = NULL;
	snapdata_collector_t* collector = NULL;
	CONTAINER_SL_FOREACH_BEGIN( SnapdataCollectors, content )
	{
		collector = (snapdata_collector_t*)content;

		if (dev_id == collector->dev_id){
			*p_collector = collector;
			res = SUCCESS;	//don`t continue
		}
	}
	CONTAINER_SL_FOREACH_END( SnapdataCollectors );
	return res;
}


int snapdata_collect_Find( struct request_queue *q, struct bio *bio, snapdata_collector_t** p_collector )
{
	int res = -ENODATA;
	content_sl_t* content = NULL;
	snapdata_collector_t* collector = NULL;
	CONTAINER_SL_FOREACH_BEGIN( SnapdataCollectors, content )
	{
		collector = (snapdata_collector_t*)content;

		if ( (q == bdev_get_queue( collector->device ))
			&& (bio_bi_sector( bio ) >= blk_dev_get_start_sect( collector->device ))
			&& ( bio_bi_sector( bio ) < (blk_dev_get_start_sect( collector->device ) + blk_dev_get_capacity( collector->device )))
		){
			*p_collector = collector;
			res = SUCCESS;	//don`t continue
		}
	}
	CONTAINER_SL_FOREACH_END( SnapdataCollectors );
	return res;
}


int _snapdata_collect_bvec( snapdata_collector_t* collector, sector_t ofs, struct bio_vec* bvec )
{
	unsigned int bv_len;
	unsigned int bv_offset;
	sector_t buff_ofs;
	void* mem;
	stream_size_t sectors_map = 0;
	bv_len = bvec->bv_len;
	bv_offset = bvec->bv_offset;

	if ((bv_len >> SECTOR512_SHIFT) > (sizeof( stream_size_t ) * 8)){ //because sectors_map have only 64 or 32 bits.
		log_errorln_d( "Not supported big PAGE_SIZE yet. bv_len=", bv_len );
		return -EINVAL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	mem = kmap_atomic( bvec->bv_page, KM_BOUNCE_READ );
#else
	mem = kmap_atomic( bvec->bv_page ) ;
#endif
	for (buff_ofs = bv_offset; buff_ofs < ( bv_offset + bv_len ); buff_ofs+=SECTOR512){
		size_t compare_len = min( (size_t)SECTOR512, collector->magic_size );

		if (0 == memcmp( mem + buff_ofs, collector->magic_buff, compare_len )){
			sectors_map |= (stream_size_t)1 << (stream_size_t)(buff_ofs >> SECTOR512_SHIFT);
			collector->collected_size += SECTOR512;
		}
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	kunmap_atomic( mem, KM_BOUNCE_READ );
#else
	kunmap_atomic( mem );
#endif


	for (buff_ofs = bv_offset; buff_ofs < (bv_offset + bv_len); buff_ofs += SECTOR512){
		sector_t buff_ofs_sect = sector_from_size( buff_ofs );
		if (((stream_size_t)1 << (stream_size_t)buff_ofs_sect) & sectors_map){
			int res = SUCCESS;
#ifdef SNAPDATA_SPARSE_CHANGES
			res = sparsebitmap_Set( &collector->changes_sparse, (ofs + buff_ofs_sect), true );
#else
			{
				size_t index = ofs + buff_ofs_sect - collector->start_index;
				res = page_array_bit_set( collector->changes, index, true );
			}
#endif
			if (res != SUCCESS){
				log_errorln_sect( "sector# ", (ofs + buff_ofs_sect) );
				if (res == -EALREADY){
					log_errorln( "already set" );
				}else{
					log_errorln_d( "error code=", res );
					return res;
				}
			}
		}
	}
	return SUCCESS;
}


void snapdata_collect_Process( snapdata_collector_t* collector, struct bio *bio )
{
	sector_t ofs;
	sector_t size;

	if (unlikely(bio_data_dir( bio ) == READ))//read do not process
		return;

	if (unlikely(collector->fail_code != SUCCESS))
		return;

	ofs = bio_bi_sector( bio ) - blk_dev_get_start_sect( collector->device );
	size = bio_sectors( bio );

	{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
		struct bio_vec* bvec;
		unsigned short iter;
#else
		struct bio_vec bvec;
		struct bvec_iter iter;
#endif
		bio_for_each_segment( bvec, bio, iter ) {

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
			int err = _snapdata_collect_bvec( collector, ofs, bvec );
			ofs += sector_from_size( bvec->bv_len );
#else
			int err = _snapdata_collect_bvec( collector, ofs, &bvec );
			ofs += sector_from_size( bvec.bv_len );
#endif
			if (err){
				collector->fail_code = err;
				log_errorln_d( "Failed to collect bio. fail_code=", collector->fail_code );
				break;
			}
		}
	}
}

