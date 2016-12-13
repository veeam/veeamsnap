#pragma once
#ifndef DIRECT_DEVICE_H_
#define DIRECT_DEVICE_H_


int direct_device_init( void );

int direct_device_done( void );

int direct_device_open( dev_t dev_id );

int direct_device_close( dev_t dev_id );

int direct_device_read4user( dev_t dev_id, stream_size_t offset, char* user_buffer, size_t length );


#endif//DIRECT_DEVICE_H_
