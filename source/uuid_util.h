#pragma once

#if LINUX_VERSION_CODE < KERNEL_VERSION( 4, 13, 0 )

#define UUID_SIZE 16

typedef struct uuid_s
{
	__u8 b[UUID_SIZE];
}uuid_t;

static inline void uuid_copy( uuid_t* dst, uuid_t* src )
{
	memcpy( dst->b, src->b, UUID_SIZE );
};

static inline bool uuid_equal( uuid_t* first, uuid_t* second )
{
	return (0 == memcmp( first->b, second->b, UUID_SIZE ));
};

static inline void uuid_gen( unsigned char uuid[UUID_SIZE] )
{
	get_random_bytes( uuid, UUID_SIZE );
};

#else

#include <linux/uuid.h>
#define uuid_gen generate_random_uuid

#endif 


