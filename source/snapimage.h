#pragma once
#include "veeamsnap_ioctl.h"


#define SNAPIMAGE_MAX_DEVICES 2048

int snapimage_init( void );
int snapimage_done( void );
int snapimage_create_for( dev_t* p_dev, int count );
int snapimage_stop( dev_t original_dev );
int snapimage_destroy( dev_t original_dev );

int snapimage_collect_images( int count, struct image_info_s* p_user_image_info, int* p_real_count );

void snapimage_print_state( void );

