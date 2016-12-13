#pragma once

#ifndef SNAPSHOT_H
#define SNAPSHOT_H

typedef struct snapshot_map_s
{
	dev_t DevId;
}snapshot_map_t;

typedef struct snapshot_s
{
	content_t content;
	unsigned long long id;
	int snapshot_map_length;
	snapshot_map_t* p_snapshot_map;    //array
}snapshot_t;

int snapshot_Init( void );
int snapshot_Done( void );

int snapshot_FindById( unsigned long long id, snapshot_t** pp_snapshot );

int snapshot_Create( dev_t* p_dev, int count, unsigned int cbt_block_size_degree, unsigned long long* p_snapshot_id );

int snapshot_Destroy( unsigned long long snapshot_id );

#endif//SNAPSHOT_H
