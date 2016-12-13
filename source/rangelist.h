#pragma once

#ifndef RANGELIST_H_
#define RANGELIST_H_

typedef struct range_content_s
{
	content_t content;
	range_t rg;
}range_content_t;

typedef struct rangelist_s
{
	container_t list;
	char cache_name[15 + 16 + 1];
}rangelist_t;


int rangelist_Init( rangelist_t* rglist );

void rangelist_Done( rangelist_t* rglist );

int rangelist_Add( rangelist_t* rglist, range_t* rg );

int rangelist_Get( rangelist_t* rglist, range_t* rg );

static inline size_t rangelist_Length( rangelist_t* rglist )
{
	return (size_t)container_length( &rglist->list );
};

#endif //RANGELIST_H_