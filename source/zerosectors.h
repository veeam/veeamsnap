#pragma once

#ifdef SNAPDATA_ZEROED

#include "snapshotdata_file.h"
#include "rangevector.h"
#include "veeamsnap_ioctl.h"

int zerosectors_add_ranges( rangevector_t* zero_sectors, page_array_t* ranges, size_t ranges_cnt );

#ifndef SNAPSTORE
int zerosectors_add_file( rangevector_t* zero_sectors, dev_t dev_id, snapshotdata_file_t* file );
#endif //SNAPSTORE

#endif //SNAPDATA_ZEROED

