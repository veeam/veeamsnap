// Copyright (c) Veeam Software Group GmbH

#include "stdafx.h"

#ifdef PERSISTENT_CBT

#include "ext4_check.h"
#include "blk_util.h"
#include <linux/crc32.h>
#include <linux/buffer_head.h>

#define SECTION "ext4_chkfs"
#include "log_format.h"


// from vanilla kernel 2.6.32.9
/*
 * Structure of the super block
 */
struct ext4_super_block {
/*00*/  __le32  s_inodes_count;        /* Inodes count */
        __le32  s_blocks_count_lo;    /* Blocks count */
        __le32  s_r_blocks_count_lo;    /* Reserved blocks count */
        __le32  s_free_blocks_count_lo;    /* Free blocks count */
/*10*/  __le32  s_free_inodes_count;    /* Free inodes count */
        __le32  s_first_data_block;    /* First Data Block */
        __le32  s_log_block_size;    /* Block size */
        __le32  s_obso_log_frag_size;    /* Obsoleted fragment size */
/*20*/  __le32  s_blocks_per_group;    /* # Blocks per group */
        __le32  s_obso_frags_per_group;    /* Obsoleted fragments per group */
        __le32  s_inodes_per_group;    /* # Inodes per group */
        __le32  s_mtime;        /* Mount time */
/*30*/  __le32  s_wtime;        /* Write time */
        __le16  s_mnt_count;        /* Mount count */
        __le16  s_max_mnt_count;    /* Maximal mount count */
        __le16  s_magic;        /* Magic signature */
        __le16  s_state;        /* File system state */
        __le16  s_errors;        /* Behaviour when detecting errors */
        __le16  s_minor_rev_level;    /* minor revision level */
/*40*/  __le32  s_lastcheck;        /* time of last check */
        __le32  s_checkinterval;    /* max. time between checks */
        __le32  s_creator_os;        /* OS */
        __le32  s_rev_level;        /* Revision level */
/*50*/  __le16  s_def_resuid;        /* Default uid for reserved blocks */
        __le16  s_def_resgid;        /* Default gid for reserved blocks */
    /*
     * These fields are for EXT4_DYNAMIC_REV superblocks only.
     *
     * Note: the difference between the compatible feature set and
     * the incompatible feature set is that if there is a bit set
     * in the incompatible feature set that the kernel doesn't
     * know about, it should refuse to mount the filesystem.
     *
     * e2fsck's requirements are more strict; if it doesn't know
     * about a feature in either the compatible or incompatible
     * feature set, it must abort and not try to meddle with
     * things it doesn't understand...
     */
        __le32  s_first_ino;        /* First non-reserved inode */
        __le16  s_inode_size;        /* size of inode structure */
        __le16  s_block_group_nr;    /* block group # of this superblock */
        __le32  s_feature_compat;    /* compatible feature set */
/*60*/  __le32  s_feature_incompat;    /* incompatible feature set */
        __le32  s_feature_ro_compat;    /* readonly-compatible feature set */
/*68*/  __u8    s_uuid[16];        /* 128-bit uuid for volume */
/*78*/  char    s_volume_name[16];    /* volume name */
/*88*/  char    s_last_mounted[64];    /* directory where last mounted */
/*C8*/  __le32  s_algorithm_usage_bitmap; /* For compression */
    /*
     * Performance hints.  Directory preallocation should only
     * happen if the EXT4_FEATURE_COMPAT_DIR_PREALLOC flag is on.
     */
        __u8    s_prealloc_blocks;    /* Nr of blocks to try to preallocate*/
        __u8    s_prealloc_dir_blocks;    /* Nr to preallocate for dirs */
        __le16  s_reserved_gdt_blocks;    /* Per group desc for online growth */
    /*
     * Journaling support valid if EXT4_FEATURE_COMPAT_HAS_JOURNAL set.
     */
/*D0*/  __u8    s_journal_uuid[16];    /* uuid of journal superblock */
/*E0*/  __le32  s_journal_inum;        /* inode number of journal file */
        __le32  s_journal_dev;        /* device number of journal file */
        __le32  s_last_orphan;        /* start of list of inodes to delete */
        __le32  s_hash_seed[4];        /* HTREE hash seed */
        __u8    s_def_hash_version;    /* Default hash version to use */
        __u8    s_reserved_char_pad;
        __le16  s_desc_size;        /* size of group descriptor */
/*100*/ __le32  s_default_mount_opts;
        __le32  s_first_meta_bg;    /* First metablock block group */
        __le32  s_mkfs_time;        /* When the filesystem was created */
        __le32  s_jnl_blocks[17];    /* Backup of the journal inode */
    /* 64bit support valid if EXT4_FEATURE_COMPAT_64BIT */
/*150*/ __le32  s_blocks_count_hi;    /* Blocks count */
        __le32  s_r_blocks_count_hi;    /* Reserved blocks count */
        __le32  s_free_blocks_count_hi;    /* Free blocks count */
        __le16  s_min_extra_isize;    /* All inodes have at least # bytes */
        __le16  s_want_extra_isize;     /* New inodes should reserve # bytes */
        __le32  s_flags;        /* Miscellaneous flags */
        __le16  s_raid_stride;        /* RAID stride */
        __le16  s_mmp_interval;         /* # seconds to wait in MMP checking */
        __le64  s_mmp_block;            /* Block for multi-mount protection */
        __le32  s_raid_stripe_width;    /* blocks on all data disks (N*stride)*/
        __u8    s_log_groups_per_flex;  /* FLEX_BG group size */
        __u8    s_reserved_char_pad2;
        __le16  s_reserved_pad;
        __le64  s_kbytes_written;    /* nr of lifetime kilobytes written */
        __u32   s_reserved[160];        /* Padding to the end of the block */
};

#define EXT4_SUPER_MAGIC	0xEF53
#define EXT4_MIN_BLOCK_SIZE		1024

/*

static int _ext4_get_sb(struct block_device* bdev, struct ext4_super_block **p_sb )
{
    int res;
    struct page* pg = alloc_page(GFP_KERNEL);
    void* addr;
    if (pg == NULL){
        log_err("Failed to allocate page");
        return -ENOMEM;
    }
    addr = page_address(pg);

    do{
        struct ext4_super_block* sb = NULL;
        res = blk_direct_submit_page(bdev, READ_SYNC, 0, pg);//read first page
        if (res != SUCCESS){
            log_err("Failed to read the first page from the device.");
            break;
        }

        struct ext4_super_block* sb = dbg_kmalloc(sizeof(struct ext4_super_block), GFP_KERNEL);
        if (sb == NULL){
            res = -ENOMEM;
            break;
        }

        memcpy(sb, addr+2*SECTOR512, sizeof(struct ext4_super_block));

        if (le16_to_cpu(sb->s_magic) != EXT4_SUPER_MAGIC){
            log_tr("Ext fs not found.");
            dbg_kfree(sb);
            res = ENOENT;
            break;
        }

        *p_sb = sb;
        res = SUCCESS;
    } while (false);
    free_page(addr);

    return res;
}
*/

static int _ext4_get_sb(struct block_device* bdev, struct ext4_super_block **p_sb)
{
    int res;
    struct buffer_head *bh = NULL;
    struct ext4_super_block *es = NULL;
    unsigned long long sb_block;
    unsigned long offset;
    unsigned blocksize = blk_dev_get_block_size(bdev);

    sb_block = EXT4_MIN_BLOCK_SIZE / blocksize;
    offset = EXT4_MIN_BLOCK_SIZE % blocksize;

    bh = __bread(bdev, sb_block, blocksize);
    if (bh == NULL) {
        log_err("Failed to read superblock");
        return -ENOMEM;
    }
    es = (struct ext4_super_block *)(bh->b_data + offset);
    do{
        struct ext4_super_block* sb = dbg_kmalloc(sizeof(struct ext4_super_block), GFP_KERNEL);
        if (sb == NULL){
            res = -ENOMEM;
            break;
        }
        if (le16_to_cpu(es->s_magic) != EXT4_SUPER_MAGIC){
            log_tr("Ext fs not found");
            dbg_kfree(sb);
            res = ENOENT;
            break;
        }

        memcpy(sb, es, sizeof(struct ext4_super_block));
        *p_sb = sb;
        res = SUCCESS;
    } while (false);
    brelse(bh);

    return res;
}



int ext4_check_offline_changes(struct block_device* bdev, uint32_t previous_crc)
{
    struct ext4_super_block* sb = NULL;
    int res = _ext4_get_sb(bdev, &sb);
    if (res == ENOENT)
        return res;

    if (res != SUCCESS){
        log_err("Failed to read superblock");
        return res;
    }

    //EXT file system found!
    log_tr_uuid("128-bit uuid for volume: ", (veeam_uuid_t*)&sb->s_uuid);
    log_tr_s("volume name: ", sb->s_volume_name);
    log_tr_d("File system state: ", le16_to_cpu(sb->s_state));

    log_tr_s_sec("Mount time: ", le32_to_cpu(sb->s_mtime));
    log_tr_s_sec("Write time: ", le32_to_cpu(sb->s_wtime));
    log_tr_s_sec("Time of last check: ", le32_to_cpu(sb->s_lastcheck));
    {
        uint32_t crc = crc32(~0, (void*)(sb), sizeof(struct ext4_super_block));

        if (crc == previous_crc){
            log_tr("Superblock crc match.");
            res = SUCCESS;
        }else{
            log_tr("Superblock crc mismatch.");
            log_tr_lx("Previous crc: ", previous_crc);
            log_tr_lx("Current crc: ", crc);
            res = ENOENT;
        }
    }
    dbg_kfree(sb);

    return res;
}

int ext4_check_unmount_status(struct block_device* bdev, uint32_t* p_sb_crc)
{
    struct ext4_super_block* sb = NULL;
    int res = _ext4_get_sb(bdev, &sb);
    if (res == ENOENT)
        return res;

    if (res != SUCCESS){
        log_err("Failed to read superblock");
        return res;
    }

    //EXT file system found!
    log_tr_uuid("128-bit uuid for volume: ", (veeam_uuid_t*)&sb->s_uuid );
    log_tr_s("volume name: ", sb->s_volume_name);
    log_tr_d("File system state: ", le16_to_cpu(sb->s_state));

    log_tr_s_sec("Mount time: ", le32_to_cpu(sb->s_mtime));
    log_tr_s_sec("Write time: ", le32_to_cpu(sb->s_wtime));
    log_tr_s_sec("Time of last check: ", le32_to_cpu(sb->s_lastcheck));
    {
        uint32_t crc = crc32(~0, (void*)(sb), sizeof(struct ext4_super_block));
        *p_sb_crc = crc;
        res = SUCCESS;
    }
    dbg_kfree(sb);

    return res;
}

#endif //PERSISTENT_CBT
