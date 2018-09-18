#pragma once
#include "veeamsnap_ioctl.h"

void tracking_Init(void);

int tracking_Done(void);

int tracking_add( dev_t dev_id, unsigned int cbt_block_size_degree );

int tracking_remove(dev_t dev_id);

int tracking_collect( int max_count, struct cbt_info_s* p_cbt_info, int* p_count );

int tracking_read_cbt_bitmap( dev_t dev_id, unsigned int offset, size_t length, void* user_buff );

