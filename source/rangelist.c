#include "stdafx.h"
#include "range.h"
#include "container.h"
#include "rangelist.h"


int rangelist_Init( rangelist_t* rglist )
{

	sprintf( rglist->cache_name, "vsnap_Rangelist%p", rglist );
	log_traceln_s( "cache creating with name=", rglist->cache_name );

	return container_init( &rglist->list, sizeof( range_content_t ), rglist->cache_name );

}

void rangelist_Done( rangelist_t* rglist )
{
	content_t* cont;

	while (NULL != (cont = container_get_first( &rglist->list )) )
		content_free( cont );

	container_done( &rglist->list );
}

int rangelist_Add( rangelist_t* rglist, range_t* rg )
{
	content_t* cont;
	range_content_t* range_ct;

	cont = content_new( &rglist->list );
	if (cont == NULL)
		return -ENOMEM;

	range_ct = (range_content_t*)cont;
	range_ct->rg.ofs = rg->ofs;
	range_ct->rg.cnt = rg->cnt;

	container_push_back( &rglist->list, cont );
	return SUCCESS;
}

int rangelist_Get( rangelist_t* rglist, range_t* rg )
{
	content_t* cont;
	range_content_t* range_ct;

	cont = container_get_first( &rglist->list );
	if (cont == NULL){
		return -ENODATA;
	}
	range_ct = (range_content_t*)cont;
	rg->ofs = range_ct->rg.ofs;
	rg->cnt = range_ct->rg.cnt;

	content_free( cont );

	return SUCCESS;
}
