#ifndef VEEAMSNAP_IOCTL_H_
#define VEEAMSNAP_IOCTL_H_

#define SUCCESS 0

#define MAX_TRACKING_DEVICE_COUNT	256

#define VEEAM_SNAP	'sV'

#pragma pack(push,1)
//////////////////////////////////////////////////////////////////////////
struct ioctl_getversion_s {
	unsigned short major;
	unsigned short minor;
	unsigned short revision;
	unsigned short build;
};
#define IOCTL_GETVERSION	_IOW(VEEAM_SNAP, 1, struct ioctl_getversion_s)


//////////////////////////////////////////////////////////////////////////
struct ioctl_dev_id_s{
	int major;
	int minor;
};
#define IOCTL_TRACKING_ADD		_IOW(VEEAM_SNAP, 2, struct ioctl_dev_id_s)
//////////////////////////////////////////////////////////////////////////
#define IOCTL_TRACKING_REMOVE	_IOW(VEEAM_SNAP, 3, struct ioctl_dev_id_s)
//////////////////////////////////////////////////////////////////////////
struct cbt_info_s{
	struct ioctl_dev_id_s dev_id;
	unsigned long long dev_capacity;
	unsigned int cbt_map_size;
	unsigned char snap_number;
    unsigned char generationId[16];
};
struct ioctl_tracking_collect_s{
	unsigned int count;
	union{
		struct cbt_info_s* p_cbt_info;
		unsigned long long ull_cbt_info;
	};
};

#define IOCTL_TRACKING_COLLECT		_IOW(VEEAM_SNAP, 4, struct ioctl_tracking_collect_s)
//////////////////////////////////////////////////////////////////////////

#define IOCTL_TRACKING_BLOCK_SIZE	_IOW(VEEAM_SNAP, 5, unsigned int)
//////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_create_s{
	unsigned long long snapshot_id;
	unsigned int count;
	union{
		struct ioctl_dev_id_s* p_dev_id;
		unsigned long long ull_dev_id;
	};
};
#define IOCTL_SNAPSHOT_CREATE		_IOW(VEEAM_SNAP, 0x10, struct ioctl_snapshot_create_s)
//////////////////////////////////////////////////////////////////////////
#define IOCTL_SNAPSHOT_DESTROY		_IOR(VEEAM_SNAP, 0x11, unsigned long long )
//////////////////////////////////////////////////////////////////////////
struct ioctl_tracking_read_cbt_bitmap_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int offset;
	unsigned int length;
	union{
		unsigned char* buff;
		unsigned long long ull_buff;
	};
};
#define IOCTL_TRACKING_READ_CBT_BITMAP		_IOR(VEEAM_SNAP, 0x12, struct ioctl_tracking_read_cbt_bitmap_s)

//////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_errno_s{
	struct ioctl_dev_id_s dev_id;
	int err_code;
};
#define IOCTL_SNAPSHOT_ERRNO    _IOW(VEEAM_SNAP, 0x1B, struct ioctl_snapshot_errno_s)
///////////////////////////////////////////////////////////////////////////////
struct ioctl_range_s{
	unsigned long long left;
	unsigned long long right;
};
//////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_common_reserve_s{
	unsigned int common_file_id;
};
#define IOCTL_SNAPSHOT_COMMON_RESERVE	_IOW(VEEAM_SNAP, 0x23, struct ioctl_snapshot_common_reserve_s)
#define IOCTL_SNAPSHOT_COMMON_UNRESERVE _IOR(VEEAM_SNAP, 0x24, struct ioctl_snapshot_common_reserve_s)
//////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_common_datainfo_s{
	unsigned int common_file_id;
	struct ioctl_dev_id_s dev_id_host_data;
	unsigned int range_count;
	union{
		struct ioctl_range_s* ranges;
		unsigned long long ull_ranges;
	};
};
#define IOCTL_SNAPSHOT_COMMON_DATAINFO		_IOR(VEEAM_SNAP, 0x21, struct ioctl_snapshot_common_datainfo_s)
//////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_common_add_dev_s{
	unsigned int common_file_id;
	struct ioctl_dev_id_s dev_id;
};
#define IOCTL_SNAPSHOT_COMMON_ADD_DEV		_IOR(VEEAM_SNAP, 0x25, struct ioctl_snapshot_common_add_dev_s)
//////////////////////////////////////////////////////////////////////////
#define IOCTL_SNAPSHOT_COMMON_DATAINFO_CLEAN	_IO(VEEAM_SNAP, 0x22)
//////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_datainfo_s{
	struct ioctl_dev_id_s dev_id;
	struct ioctl_dev_id_s dev_id_host_data;
	unsigned int range_count;
	union{
		struct ioctl_range_s* ranges;
		unsigned long long ull_ranges;
	};
};
#define IOCTL_SNAPSHOT_DATAINFO		_IOR(VEEAM_SNAP, 0x15, struct ioctl_snapshot_datainfo_s)
///////////////////////////////////////////////////////////////////////////////
struct ioctl_snapshot_datainfo_memory_s
{
	struct ioctl_dev_id_s dev_id;
	unsigned long long snapshotdatasize;
};
#define IOCTL_SNAPSHOT_DATAINFO_MEMORY	_IOR(VEEAM_SNAP, 0x16, struct ioctl_snapshot_datainfo_memory_s)
//////////////////////////////////////////////////////////////////////////
#define IOCTL_SNAPSHOT_DATAINFO_CLEAN	_IOR(VEEAM_SNAP, 0x17, struct ioctl_dev_id_s)
//////////////////////////////////////////////////////////////////////////
struct ioctl_direct_device_read_s{
	struct ioctl_dev_id_s dev_id;
	unsigned long long offset;
	unsigned int length;
	union{
		void* buff;
		unsigned long long ull_buff;
	};
};
#define IOCTL_DIRECT_DEVICE_READ		_IOW(VEEAM_SNAP, 0x18, struct ioctl_direct_device_read_s)

//////////////////////////////////////////////////////////////////////////
#define  IOCTL_DIRECT_DEVICE_OPEN _IOR(VEEAM_SNAP, 0x19, struct ioctl_dev_id_s )
//////////////////////////////////////////////////////////////////////////
#define  IOCTL_DIRECT_DEVICE_CLOSE _IOR(VEEAM_SNAP, 0x1A, struct ioctl_dev_id_s )
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
struct ioctl_get_block_size_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int block_size;
};
#define  IOCTL_GET_BLOCK_SIZE _IOW(VEEAM_SNAP, 0x20, struct ioctl_get_block_size_s )
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
struct ioctl_collect_snapshotdata_location_start_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int magic_length;
	union{
		void* magic_buff;
		unsigned long long ull_buff;
	};
};
#define  IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_START _IOW(VEEAM_SNAP, 0x40, struct ioctl_collect_snapshotdata_location_start_s )
//////////////////////////////////////////////////////////////////////////
struct ioctl_collect_snapshotdata_location_get_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int range_count;     //
	union{
		struct ioctl_range_s* ranges;
		unsigned long long ull_ranges;
	};
};
#define IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_GET		_IOW(VEEAM_SNAP, 0x41, struct ioctl_collect_snapshotdata_location_get_s)

//////////////////////////////////////////////////////////////////////////
struct ioctl_collect_snapshotdata_location_complete_s{
	struct ioctl_dev_id_s dev_id;
};
#define  IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_COMPLETE _IOR(VEEAM_SNAP, 0x42, struct ioctl_collect_snapshotdata_location_complete_s )
//////////////////////////////////////////////////////////////////////////
struct image_info_s{
	struct ioctl_dev_id_s original_dev_id;
	struct ioctl_dev_id_s snapshot_dev_id;
};

struct ioctl_collect_shapshot_images_s{
	int count;     //
	union{
		struct image_info_s* p_image_info;
		unsigned long long ull_image_info;
	};
};
#define IOCTL_COLLECT_SNAPSHOT_IMAGES _IOW(VEEAM_SNAP, 0x50, struct ioctl_collect_shapshot_images_s)
//////////////////////////////////////////////////////////////////////////


#define IOCTL_PRINTSTATE _IO(VEEAM_SNAP, 0x80)

//////////////////////////////////////////////////////////////////////////
#pragma pack(pop)

#define VEEAMSNAP_SI_PRINT (SI_USER+0)
#define VEEAMSNAP_SI_SNAPFILL (SI_USER+1)
#define VEEAMSNAP_SI_SNAPOVERFLOW (SI_USER+2)

// commands for character device interface
#define VEEAMSNAP_CHARCMD_UNDEFINED 0x00
#define VEEAMSNAP_CHARCMD_ACKNOWLEDGE 0x01
#define VEEAMSNAP_CHARCMD_INVALID 0xFF
// to module commands
#define VEEAMSNAP_CHARCMD_INITIATE 0x21
#define VEEAMSNAP_CHARCMD_NEXT_PORTION 0x22
// from module commands
#define VEEAMSNAP_CHARCMD_HALFFILL 0x41
#define VEEAMSNAP_CHARCMD_OVERFLOW 0x42
#define VEEAMSNAP_CHARCMD_TERMINATE 0x43


#endif /* VEEAMSNAP_IOCTL_H_ */

