#ifndef POMELO_FS_H
#define POMELO_FS_H

#include "../../kernel/types.h"
#include "../../kernel/ipc/ipc.h"
#include "format.h"

/* fs <-> shell protocol. Shell is the client, fs is the server: one
 * request message, one reply message with the same type. */
#define MSG_FS_LS     1   /* -> one entry per used inode (arg0=size,
                           data=name), then arg0=-1 as terminator */
#define MSG_FS_OPEN   2   /* data=name -> arg0=inode|-1, arg1=size */
#define MSG_FS_READ   3   /* arg0=inode, arg1=off -> arg0=len, data */
#define MSG_FS_CREATE 4   /* data=name -> arg0=inode|-1 */
#define MSG_FS_WRITE  5   /* arg0=inode, arg1=off, arg2=len, data -> arg0=len */
#define MSG_FS_COMMIT 6   /* arg0=inode, arg1=size -> arg0=0 */

/* ata.c: polled PIO, one sector at a time. */
int ata_read(u32 lba, void *buf);
int ata_write(u32 lba, const void *buf);

/* fs.c: shared state and the reply helper. */
extern struct superblock sb;
extern struct inode inodes[NINODES];
void reply(struct msg *q, int a0, int a1, int a2, const char *d, int dl);

#endif
