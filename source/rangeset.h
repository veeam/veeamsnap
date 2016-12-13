
#pragma once

#ifndef RANGESET_H_
#define RANGESET_H_

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

	typedef struct rangeset_s
	{
		union{
			size_t range_count;
			stream_size_t _range_count;
		};

		range_t ranges[0];
	}rangeset_t;

	int rangeset_create( size_t range_count, rangeset_t** p_rangeset );

	void rangeset_destroy( rangeset_t* rangeset );

	int rangeset_set( rangeset_t* rangeset, size_t inx, range_t rg );

	int rangeset_v2p( rangeset_t* rangeset, sector_t virt_offset, sector_t virt_length, sector_t* p_phys_offset, sector_t* p_phys_length );

	sector_t rangeset_length( rangeset_t* rangeset );

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif //RANGESET_H_
