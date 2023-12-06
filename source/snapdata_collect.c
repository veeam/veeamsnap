// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"
#include "snapdata_collect.h"
#include "blk_util.h"

#define SECTION "snapdatact"
#include "log_format.h"

struct collector_list
{
    struct list_head headList;
    rwlock_t lock;
};

static struct collector_list ActiveCollectors;
static struct collector_list StoppedCollectors;

static inline void collector_list_init(struct collector_list* lst)
{
    INIT_LIST_HEAD(&lst->headList);
    rwlock_init(&lst->lock);
};

static inline snapdata_collector_t* collector_list_first(struct collector_list* lst)
{
    snapdata_collector_t* collector = NULL;

    read_lock(&lst->lock);
    if (!list_empty(&lst->headList))
        collector = list_entry(lst->headList.next, snapdata_collector_t, link);
    read_unlock(&lst->lock);

    return collector;
}

static inline snapdata_collector_t* collector_list_get_first(struct collector_list* lst)
{
    snapdata_collector_t* collector = NULL;

    write_lock(&lst->lock);
    if (!list_empty(&lst->headList)) {
        collector = list_entry(lst->headList.next, snapdata_collector_t, link);
        if (collector)
            list_del(&collector->link);
    }
    write_unlock(&lst->lock);

    return collector;
}

static inline void collector_list_push_back(struct collector_list* lst, snapdata_collector_t* collector)
{
    write_lock(&lst->lock);
    list_add_tail(&collector->link, &lst->headList);
    write_unlock(&lst->lock);
}

static inline void collector_list_pull(struct collector_list* lst, snapdata_collector_t* collector)
{
    write_lock(&lst->lock);
    list_del(&collector->link);
    write_unlock(&lst->lock);
}

static int collector_init( snapdata_collector_t* collector, dev_t dev_id, void* MagicUserBuff, size_t MagicLength );
static void collector_free( snapdata_collector_t* collector );
static void collector_stop(snapdata_collector_t* collector);
static snapdata_collector_t* collector_get(dev_t dev_id, bool pull);

int snapdata_collect_Init( void )
{
    collector_list_init(&ActiveCollectors);
    collector_list_init(&StoppedCollectors);

    return SUCCESS;
}

void snapdata_collect_Done( void )
{
    snapdata_collector_t* collector = NULL;

    while ((collector = collector_list_first(&ActiveCollectors)))
        collector_stop(collector);

    while ((collector = collector_list_get_first(&StoppedCollectors)))
        collector_free(collector);
}

static int collector_init( snapdata_collector_t* collector, dev_t dev_id, void* MagicUserBuff, size_t MagicLength )
{
    int res = SUCCESS;

    INIT_LIST_HEAD(&collector->link);
    collector->fail_code = SUCCESS;
    collector->dev_id = dev_id;

    res = blk_dev_open( collector->dev_id, &collector->device );
    if (res != SUCCESS){
        log_err_format( "Unable to initialize snapstore collector: failed to open device [%d:%d]. errno=%d", MAJOR( collector->dev_id ), MINOR( collector->dev_id ), res );
        return res;
    }

    collector->magic_size = MagicLength;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,13,0)
    collector->magic_buff = dbg_kmalloc( collector->magic_size, GFP_KERNEL | __GFP_REPEAT );
#else
    collector->magic_buff = dbg_kmalloc( collector->magic_size, GFP_KERNEL | __GFP_RETRY_MAYFAIL );
#endif
    if (collector->magic_buff == NULL){
        log_err( "Unable to initialize snapstore collector: not enough memory" );
        return -ENOMEM;
    }
    if (0 != copy_from_user( collector->magic_buff, MagicUserBuff, collector->magic_size )){
        log_err( "Unable to initialize snapstore collector: invalid user buffer" );
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

        log_tr_lld( "Create bitmap for snapstore collector. Size ", bitmap_size );

        collector->start_index = 0;
        collector->length = blk_dev_get_capacity( collector->device );

        collector->changes = page_array_alloc( page_count, GFP_KERNEL );
        if (collector->changes == NULL){
            return -ENOMEM;
        }
        page_array_memset( collector->changes, 0 );
    }
#endif

    mutex_init(&collector->locker);
    return res;
}

static int _collector_start(snapdata_collector_t* collector)
{
    int res = SUCCESS;

#ifdef VEEAMSNAP_BLK_FREEZE
    struct super_block* sb = NULL;
    res = blk_freeze_bdev( collector->dev_id, collector->device, &sb);
#else
    res = freeze_bdev(collector->device);
#endif
    if (res)
        return res;

#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
    res = tracker_disk_create_or_get(collector->device->bd_disk, &collector->tr_disk);
#else
    res = tracker_disk_create_or_get(collector->device->bd_disk->queue, &collector->tr_disk);
#endif
    if (res == SUCCESS)
        collector_list_push_back(&ActiveCollectors, collector);
    else
        log_err("Unable to initialize snapstore collector: failed to reference tracker disk");

#ifdef VEEAMSNAP_BLK_FREEZE
    sb = blk_thaw_bdev(collector->dev_id, collector->device, sb);
#else
    if (thaw_bdev(collector->device) != SUCCESS)
        log_err("Failed to thaw block device");
#endif

    return res;
}

static void collector_stop( snapdata_collector_t* collector )
{
    int res;
#ifdef VEEAMSNAP_BLK_FREEZE
    struct super_block* sb = NULL;
#endif

    if (!collector->tr_disk)
        return;

#ifdef VEEAMSNAP_BLK_FREEZE
    res = blk_freeze_bdev(collector->dev_id, collector->device, &sb);
#else
    res = freeze_bdev(collector->device);
#endif
    if (res != SUCCESS)
        log_err("Failed to treeze block device");

    tracker_disk_put( collector->tr_disk );
    collector->tr_disk = NULL;

    collector_list_pull(&ActiveCollectors, collector);
    collector_list_push_back(&StoppedCollectors, collector);

#ifdef VEEAMSNAP_BLK_FREEZE
    sb = blk_thaw_bdev(collector->dev_id, collector->device, sb);
#else
    res = thaw_bdev(collector->device);
    if (res != SUCCESS)
        log_err("Failed to thaw block device");
#endif
}


static void collector_free( snapdata_collector_t* collector )
{
#ifdef SNAPDATA_SPARSE_CHANGES
    sparsebitmap_destroy( &collector->changes_sparse );
#else
    if (collector->changes != NULL)
        page_array_free( collector->changes );
#endif
    if (collector->magic_buff != NULL){
        dbg_kfree( collector->magic_buff );
        collector->magic_buff = NULL;
    }

    if (collector->device != NULL){
        blk_dev_close( collector->device );
        collector->device = NULL;
    }

    dbg_kfree(collector);
}


int snapdata_collect_LocationStart( dev_t dev_id, void* MagicUserBuff, size_t MagicLength )
{
    snapdata_collector_t* collector = NULL;
    int res = -ENOTSUPP;

    log_tr_dev_t( "Start collecting snapstore data location on device ", dev_id );

    collector = dbg_kzalloc(sizeof(snapdata_collector_t), GFP_KERNEL);
    if (NULL == collector){
        log_err( "Unable to start collecting snapstore data location: not enough memory" );
        return  -ENOMEM;
    }

    res = collector_init( collector, dev_id, MagicUserBuff, MagicLength );
    if (res)
        goto fail;

    res = _collector_start(collector);
    if (res)
        goto fail;

    return SUCCESS;

fail:
    collector_free(collector);

    return res;
}

static void rangelist_calculate(rangelist_t* rangelist, sector_t *ranges_length, size_t *count, bool make_output)
{
    //calculate and show information about ranges
    range_t* rg;
    RANGELIST_FOREACH_BEGIN((*rangelist), rg)
    {
        *ranges_length += rg->cnt;
        ++*count;
        if (make_output){
            log_tr_sect("  ofs=", rg->ofs);
            log_tr_sect("  cnt=", rg->cnt);
        }
    }
    RANGELIST_FOREACH_END();
    if (make_output){
        log_tr_sz("range_count=", *count);
        log_tr_sect("ranges_length=", *ranges_length);
    }
}

#ifndef SNAPDATA_SPARSE_CHANGES
static int page_array_convert2rangelist(page_array_t* changes, rangelist_t* rangelist, stream_size_t start_index, stream_size_t length)
{
    int res = SUCCESS;
    range_t rg = { 0 };
    size_t index = 0;

    while (index < length){
        bool bit;
        res = page_array_bit_get(changes, index, &bit);
        if (res != SUCCESS)
            break;

        if (bit){
            if (rg.cnt == 0)
                rg.ofs = start_index + index;
            ++rg.cnt;
        }
        else{
            if (rg.cnt == 0){
                // nothing
            }
            else{
                res = rangelist_add(rangelist, &rg);
                rg.cnt = 0;
            }
        }
        ++index;
    }

    if ((res == SUCCESS) && (rg.cnt != 0)){
        res = rangelist_add(rangelist, &rg);
        rg.cnt = 0;
    }
    return res;
}
#endif

int snapdata_collect_LocationGet( dev_t dev_id, rangelist_t* rangelist, size_t* ranges_count )
{
    size_t count = 0;
    sector_t ranges_length = 0;
    snapdata_collector_t* collector = NULL;
    stream_size_t collected_size;
    stream_size_t already_set_size;
    int res;

    log_tr( "Get snapstore data location");
    collector = collector_get(dev_id, false);
    if (!collector){
        log_err_dev_t( "Unable to get snapstore data location: cannot find collector for device ", dev_id );
        return -ENODEV;
    }

    if (collector->fail_code != SUCCESS){
        log_err_d( "Unable to get snapstore data location: collecting failed with errno=", 0-collector->fail_code );
        return collector->fail_code;
    }

    mutex_lock(&collector->locker);
    {
#ifdef SNAPDATA_SPARSE_CHANGES
    res = sparsebitmap_convert2rangelist(&collector->changes_sparse, rangelist, collector->changes_sparse.start_index);
#else
    res = page_array_convert2rangelist(collector->changes, rangelist, collector->start_index, collector->length);
#endif
    collected_size = atomic64_read(&collector->collected_size);
    already_set_size = atomic64_read(&collector->already_set_size);
    }
    mutex_unlock(&collector->locker);

    if (res == SUCCESS){
        rangelist_calculate(rangelist, &ranges_length, &count, false);
        log_tr_lld("Collection size: ", collected_size);
        log_tr_lld("Already set size: ", already_set_size);
        log_tr_d("Ranges count: ", count);

        *ranges_count = count;
    }
    return res;
}

int snapdata_collect_LocationComplete( dev_t dev_id )
{
    snapdata_collector_t* collector = NULL;

    log_tr("Collecting snapstore data location completed");
    collector = collector_get(dev_id, true);
    if (!collector) {
        log_err_dev_t("Unable to complete collecting snapstore data location: cannot find collector for device ", dev_id);
        return -ENODEV;
    }
    collector_free( collector );

    return SUCCESS;
}

static snapdata_collector_t* collector_get( dev_t dev_id, bool pull)
{
    snapdata_collector_t* collector;

    /* Try to find active collector */
    collector = NULL;
    read_lock(&ActiveCollectors.lock);
    if (!list_empty(&ActiveCollectors.headList)) {
        snapdata_collector_t* it = NULL;

        list_for_each_entry(it, &ActiveCollectors.headList, link) {
            if (dev_id == it->dev_id) {
                collector = it;
                break;
            }
        }
    }
    read_unlock(&ActiveCollectors.lock);

    /*Stop active collector*/
    if (collector)
        collector_stop(collector);

    /* Try to find stopped collector */
    collector = NULL;
    write_lock(&StoppedCollectors.lock);
    if (!list_empty(&StoppedCollectors.headList)) {
        snapdata_collector_t* it = NULL;

        list_for_each_entry(it, &StoppedCollectors.headList, link) {
            if (dev_id == it->dev_id) {
                collector = it;
                if (pull)
                    list_del(&collector->link);
                break;
            }
        }
    }
    write_unlock(&StoppedCollectors.lock);

    return collector;
}
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
int snapdata_collect_Find(struct bio *bio, snapdata_collector_t** p_collector)
#else
int snapdata_collect_Find(struct bio *bio, struct request_queue *queue, snapdata_collector_t** p_collector)
#endif
{
    int res = -ENODATA;

    read_lock(&ActiveCollectors.lock);
    if (!list_empty(&ActiveCollectors.headList)) {
        snapdata_collector_t* it = NULL;

        list_for_each_entry(it, &ActiveCollectors.headList, link) {
#ifdef VEEAMSNAP_BDEV_BIO
            if (it->device == bio->bi_bdev)
#else
            if (
#if defined(VEEAMSNAP_DISK_SUBMIT_BIO)
                (it->device->bd_disk == bio->bi_disk)
#else
                (it->device->bd_disk->queue == queue)
#endif
                && (bio_bi_sector(bio) >= blk_dev_get_start_sect(it->device))
                && (bio_bi_sector(bio) < (blk_dev_get_start_sect(it->device) + blk_dev_get_capacity(it->device)))
                )
#endif
            {
                *p_collector = it;
                res = SUCCESS;
                break;
            }
        }
    }
    read_unlock(&ActiveCollectors.lock);

    return res;
}

static int _snapdata_collect_bvec( snapdata_collector_t* collector, sector_t ofs, struct bio_vec* bvec )
{
    int res = SUCCESS;
    unsigned int bv_len;
    unsigned int bv_offset;
    sector_t buff_ofs;
    void* mem;
    stream_size_t sectors_map = 0;
    bv_len = bvec->bv_len;
    bv_offset = bvec->bv_offset;

    if ((bv_len >> SECTOR_SHIFT) > (sizeof( stream_size_t ) * 8)){ //because sectors_map have only 64 bits.
        log_err_format( "Unable to collect snapstore data location: large PAGE_SIZE [%ld] is not supported yet. bv_len=%d", PAGE_SIZE, bv_len );
        return -EINVAL;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
    mem = kmap_atomic( bvec->bv_page, KM_BOUNCE_READ );
#else
    mem = kmap_atomic( bvec->bv_page ) ;
#endif
    for (buff_ofs = bv_offset; buff_ofs < ( bv_offset + bv_len ); buff_ofs+=SECTOR_SIZE){
        size_t compare_len = min( (size_t)SECTOR_SIZE, collector->magic_size );

        if (0 == memcmp( mem + buff_ofs, collector->magic_buff, compare_len ))
            sectors_map |= (stream_size_t)1 << (stream_size_t)(buff_ofs >> SECTOR_SHIFT);
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
    kunmap_atomic( mem, KM_BOUNCE_READ );
#else
    kunmap_atomic( mem );
#endif

    mutex_lock(&collector->locker);
    for (buff_ofs = bv_offset; buff_ofs < (bv_offset + bv_len); buff_ofs += SECTOR_SIZE){
        sector_t buff_ofs_sect = sector_from_size( buff_ofs );
        if ((1ull << buff_ofs_sect) & sectors_map)
        {
            sector_t index = ofs + buff_ofs_sect;
#ifdef SNAPDATA_SPARSE_CHANGES
            res = sparsebitmap_Set(&collector->changes_sparse, index, true);
#else
            res = page_array_bit_set(collector->changes, (index - collector->start_index), true);
#endif
            if (res == SUCCESS)
                atomic64_add(SECTOR_SIZE, &collector->collected_size);
            else {
                if (res == -EALREADY)
                    atomic64_add(SECTOR_SIZE, &collector->already_set_size);
                else {
                    log_err_format("Failed to collect snapstore data location. Sector=%lld, errno=%d", index, res);
                    break;
                }
            }
        }
    }
    mutex_unlock(&collector->locker);
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
                log_err_d( "Failed to collect snapstore data location. errno=", collector->fail_code );
                break;
            }
        }
    }
}

