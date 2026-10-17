# 07 · The disk format

Corresponding code: `servers/fs/format.h`

## Why not just learn ext4

ext4 has a journal, B+ trees, and extents — that is thirty years of engineering polishing, and learning it does not help much with understanding the concept of a "filesystem." Pomelo defines its own **three-region** format instead, readable in minutes, and it nevertheless covers the whole skeleton of a filesystem:

```
  LBA 0        LBA 1..4           LBA 5 ...
┌──────────┬───────────────────┬──────────────────────┐
│ superblock│   inode table     │     data region      │
│ (1 sector)│ 32 × 64 bytes     │ 512 bytes/block,     │
│          │                   │ allocated in order   │
└──────────┴───────────────────┴──────────────────────┘
```

A sector is 512 bytes, and that is the smallest read/write unit of the ATA hardware. LBA (Logical Block Address) is the sequential number of a sector, starting at 0.

## The superblock: the whole disk's business card

```c
struct superblock {
    u32 magic;          /* 'PML1' = 0x31504d50; if the disk is wrong, refuse to mount */
    u32 n_inodes;       /* 32 */
    u32 data_start;     /* which LBA the data region starts at = 5 */
    u32 n_data;         /* how many blocks the data region has in total */
};
```

It is hung on sector 0 of the disk. When the fs service starts it reads this first and checks `magic` — that is the first step of mounting for every real filesystem.

## The inode: a file's household registration

An inode (index node) records "which blocks this file occupies and how big it is." Our inode is a fixed 64 bytes:

```c
struct inode {
    u32 used;            /* 0 = a free slot */
    char name[NAMELEN];  /* 12 bytes; a fixed-length filename saves trouble */
    u32 size;            /* the number of bytes */
    u32 direct[NDIRECT]; /* 6 direct block pointers: 6 LBA numbers */
    u8  pad[64 - 4 - NAMELEN - 4 - NDIRECT * 4];   /* pad it out to 64 bytes */
};
```

`direct[6]` means **a file is at most 6 blocks = 3KB**. Real filesystems add indirect blocks and doubly-indirect blocks to extend this; the idea is exactly the same: when the inode cannot hold the pointers, it stores a pointer to "a block that holds pointers." Here we stop at the first level, and the 3KB limit is **written out plainly in the type**.

## The data region: take it and use it, allocated in order

When writing a file, scan forward from the head of the data region for the first all-zero block, and allocate that one; no free list is kept. The cost is that **deleting a file does not reuse the space** (in fact Pomelo has no delete command either), and the gain is that the allocation code is only a few lines. A format for self-study, with every trade-off laid out in the open.

## The budget

32 inodes × 64B = 2048B, exactly 4 sectors (LBA 1–4). Add one superblock sector, and the metadata overhead is only 5 sectors; the data region runs from LBA 5 to the end of the image (1024 blocks by default, about 512KB).

## Files of this chapter

```
servers/fs/format.h   the three structures above plus the constants, all of the format definitions
```

The disk format itself has no executable code; the next chapter looks at the [file service](/guide/fs-server) that operates it.
