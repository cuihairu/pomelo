#ifndef POMELO_FORMAT_H
#define POMELO_FORMAT_H

#include "../../kernel/types.h"

/* Pomelo's own disk layout, three regions, nothing else:
 *
 *   LBA 0        LBA 1..4                 LBA 5 ...
 *   +---------+ +----------------------+ +---------------------+
 *   | super-  | | inode table          | | data blocks         |
 *   | block   | | 32 x 64 bytes        | | 512 bytes, sequential|
 *   +---------+ +----------------------+ +---------------------+
 *
 * Design rules (each is a teaching trade-off, see docs/guide/disk-format):
 *  - files are limited to NDIRECT blocks: no indirect blocks;
 *  - no free bitmap: an all-zero block is free, deletion is unsupported;
 *  - names are fixed length and there is exactly one root directory. */

#define SECTOR 512
#define FS_MAGIC 0x31504D50u      /* 'PML1' little-endian */

#define NINODES       32
#define INODE_START   1           /* inode table LBA */
#define INODE_SECTORS 4           /* 32 * 64B = 2KB = 4 sectors */
#define DATA_START    (INODE_START + INODE_SECTORS)

#define NAMELEN  12
#define NDIRECT  6
#define MAXFILE  (NDIRECT * SECTOR)

struct superblock {
    u32 magic;
    u32 n_inodes;
    u32 data_start;
    u32 n_data;
};

struct inode {
    u32 used;
    char name[NAMELEN];
    u32 size;
    u32 direct[NDIRECT];
    u8 pad[64 - 4 - NAMELEN - 4 - NDIRECT * 4];
};

_Static_assert(sizeof(struct inode) == 64, "inode must be exactly 64 bytes");
_Static_assert(sizeof(struct superblock) <= SECTOR, "sb fits in one sector");
_Static_assert(NINODES * 64 == INODE_SECTORS * SECTOR, "table is padded full");

#endif
