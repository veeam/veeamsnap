#ifndef RANGEVECTOR_H_
#define RANGEVECTOR_H_

//////////////////////////////////////////////////////////////////////////
//#define RANGEVECTOR_EL_CAPACITY 250
#define RANGEVECTOR_EL_CAPACITY 1000

typedef struct rangevector_el_s
{
	struct list_head link;
	atomic_t cnt;
	range_t ranges[RANGEVECTOR_EL_CAPACITY];
}rangevector_el_t;

typedef struct rangevector_s
{
	bool use_lock;
	struct list_head ranges_head;
	atomic_t blocks_cnt;

	struct rw_semaphore lock;
}rangevector_t;

//////////////////////////////////////////////////////////////////////////

void rangevector_create( rangevector_t* rangevector, bool use_lock );

void rangevector_destroy( rangevector_t* rangevector );

void rangevector_cleanup( rangevector_t* rangevector );

int rangevector_add( rangevector_t* rangevector, range_t rg );
void rangevector_sort( rangevector_t* rangevector );

int rangevector_v2p( rangevector_t* rangevector, sector_t virt_offset, sector_t virt_length, sector_t* p_phys_offset, sector_t* p_phys_length );

sector_t rangevector_length( rangevector_t* rangevector );

//////////////////////////////////////////////////////////////////////////
#define RANGEVECTOR_READ_LOCK( rangevector )\
if ((rangevector)->use_lock){\
    down_read( &(rangevector)->lock );\
}

#define RANGEVECTOR_READ_UNLOCK( rangevector )\
if ((rangevector)->use_lock){\
    up_read( &(rangevector)->lock );\
}

#define RANGEVECTOR_WRITE_LOCK( rangevector )\
if ((rangevector)->use_lock){\
    down_write( &(rangevector)->lock );\
}

#define RANGEVECTOR_WRITE_UNLOCK( rangevector )\
if ((rangevector)->use_lock){\
    up_write( &(rangevector)->lock );\
}

#define RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el )  \
{ \
	size_t el_inx = 0; \
    if (!list_empty( &(rangevector)->ranges_head )){ \
	    struct list_head* __list_head; \
		list_for_each( __list_head, &(rangevector)->ranges_head ){ \
        el = list_entry( __list_head, rangevector_el_t, link );

#define RANGEVECTOR_FOREACH_EL_END( rangevector ) \
		    el_inx += RANGEVECTOR_EL_CAPACITY; \
	    } \
	} \
}

#define RANGEVECTOR_FOREACH_BEGIN( rangevector, prange ) \
{ \
    rangevector_el_t* el; \
    size_t limit; \
	size_t inx = 0; \
	RANGEVECTOR_FOREACH_EL_BEGIN( rangevector, el ) \
	    limit = (size_t)atomic_read( &el->cnt );\
        for (inx = 0; inx < limit; ++inx){\
		    prange = &(el->ranges[inx]);

#define RANGEVECTOR_FOREACH_END( rangevector ) \
		}\
	RANGEVECTOR_FOREACH_EL_END( rangevector ) \
}

//////////////////////////////////////////////////////////////////////////

size_t rangevector_cnt( rangevector_t* rangevector );

range_t* rangevector_el_find_first_hit( rangevector_el_t* el, sector_t from_sect, sector_t to_sect );

#endif //RANGEVECTOR_H_