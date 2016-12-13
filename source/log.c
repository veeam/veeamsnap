#include "stdafx.h"


void log_dump( void* p, size_t size )
{
	unsigned char* pch = (unsigned char*)p;
	int pos = 0;
	char str[16*3+1+1];
	char* pstr = (char*)str;

	while(pos<size) {
		sprintf(pstr, "%02x ",pch[pos++]);
		pstr+=3;

		if ( 0==(pos %16) ){
			pr_err( "%s\n", str );
			pstr = str;
		}
	}
	if ( 0!=(pos %16) ){
		pr_err( "%s\n", str );
	}
}
//////////////////////////////////////////////////////////////////////////

