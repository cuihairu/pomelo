#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "format.h"
#include "fs.h"

/* The write path. Reads are pure arithmetic (inode -> block -> offset);
 * writes add allocation and a two-phase commit so a half-written file
 * never looks complete. Blocks are claimed lazily: CREATE takes only a
 * name, WRITE takes the blocks it touches -- and once written, a block
 * is no longer all-zero, so it can never be allocated twice. */

static u8 secbuf[SECTOR];

/* Free block = all-zero block. No bitmap, no free list; the image only
 * grows. Returns the block number (0-based inside the data area). */
static int alloc_block(void) {
    for (u32 b = 0; b < sb.n_data; b++) {
        ata_read(DATA_START + b, secbuf);
        int zero = 1;
        for (int i = 0; i < SECTOR; i++)
            if (secbuf[i]) { zero = 0; break; }
        if (zero) return (int)b;
    }
    return -1;
}

static void save_table(void) {
    for (int s = 0; s < INODE_SECTORS; s++)
        ata_write(INODE_START + s, (u8 *)inodes + s * SECTOR);
}

void do_read(struct msg *m) {
    struct inode *ino = &inodes[m->arg0];
    u32 off = m->arg1;
    if (off >= ino->size) { reply(m, 0, 0, 0, 0, 0); return; }
    int len = ino->size - off;
    int tail = (int)(SECTOR - off % SECTOR);
    if (len > MSG_DATA) len = MSG_DATA;
    if (len > tail) len = tail;
    u32 blk = ino->direct[off / SECTOR];
    if (blk == 0) {                             /* never written: zeros */
        memset(secbuf, 0, SECTOR);
    } else {
        ata_read(blk, secbuf);
    }
    reply(m, len, 0, 0, (char *)(secbuf + off % SECTOR), len);
}

void do_create(struct msg *m) {
    int slot = -1;
    for (int i = 0; i < NINODES; i++)
        if (!inodes[i].used) { slot = i; break; }
    if (slot < 0) { refuse(m, "no space"); return; }   /* full is a refusal */

    memset(&inodes[slot], 0, sizeof(struct inode));
    int n = strnlen(m->data, NAMELEN - 1);
    memcpy(inodes[slot].name, m->data, n);
    inodes[slot].used = 1;
    save_table();
    reply(m, slot, 0, 0, 0, 0);
}

void do_write(struct msg *m) {
    struct inode *ino = &inodes[m->arg0];
    u32 off = m->arg1;
    int len = m->arg2;
    int tail = (int)(SECTOR - off % SECTOR);
    if (len > MSG_DATA) len = MSG_DATA;
    if (len > tail) len = tail;
    if (off / SECTOR >= (u32)NDIRECT) { reply(m, 0, 0, 0, 0, 0); return; }

    if (ino->direct[off / SECTOR] == 0) {       /* claim a block lazily */
        int b = alloc_block();
        if (b < 0) { reply(m, 0, 0, 0, 0, 0); return; }
        ino->direct[off / SECTOR] = DATA_START + b;
    }
    ata_read(ino->direct[off / SECTOR], secbuf);
    memcpy(secbuf + off % SECTOR, m->data, len);
    ata_write(ino->direct[off / SECTOR], secbuf);
    reply(m, len, 0, 0, 0, 0);
}

void do_commit(struct msg *m) {               /* phase 2: publish size */
    inodes[m->arg0].size = m->arg1;
    save_table();
    reply(m, 0, 0, 0, 0, 0);
}
