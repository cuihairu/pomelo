#include "../../kernel/string.h"
#include "../../kernel/kprintf.h"
#include "../../kernel/syscall/syscall.h"
#include "format.h"
#include "fs.h"

struct superblock sb;
struct inode inodes[NINODES];

/* One reply per request, addressed back to the sender. */
void reply(struct msg *q, int a0, int a1, int a2, const char *d, int dl) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = q->type;
    r.arg0 = a0;
    r.arg1 = a1;
    r.arg2 = a2;
    if (d && dl > 0) memcpy(r.data, d, dl > MSG_DATA ? MSG_DATA : dl);
    sys_send(q->src, &r);
}

void do_read(struct msg *m);      /* fsrw.c */
void do_create(struct msg *m);
void do_write(struct msg *m);
void do_commit(struct msg *m);

static void do_ls(struct msg *m) {
    for (int i = 0; i < NINODES; i++)
        if (inodes[i].used)
            reply(m, inodes[i].size, 0, 0, inodes[i].name, NAMELEN);
    reply(m, -1, 0, 0, 0, 0);     /* end of listing */
}

static void do_open(struct msg *m) {
    for (int i = 0; i < NINODES; i++) {
        if (inodes[i].used && !strncmp(inodes[i].name, m->data, NAMELEN)) {
            reply(m, i, inodes[i].size, 0, 0, 0);
            return;
        }
    }
    reply(m, -1, 0, 0, 0, 0);
}

void fs_main(void) {
    if (ata_read(0, &sb) < 0) {
        kprintf("fs: no disk behind the ata ports, staying idle\n");
        for (;;) halt();          /* park this task; the rest lives on */
    }
    if (sb.magic != FS_MAGIC) {
        kprintf("fs: bad disk (magic=%x), staying idle\n", sb.magic);
        for (;;) halt();          /* no disk: park this task */
    }
    for (int s = 0; s < INODE_SECTORS; s++)
        ata_read(INODE_START + s, (u8 *)inodes + s * SECTOR);

    struct msg m;
    for (;;) {
        sys_recv(&m);
        switch (m.type) {
        case MSG_FS_LS:     do_ls(&m);     break;
        case MSG_FS_OPEN:   do_open(&m);   break;
        case MSG_FS_READ:   do_read(&m);   break;
        case MSG_FS_CREATE: do_create(&m); break;
        case MSG_FS_WRITE:  do_write(&m);  break;
        case MSG_FS_COMMIT: do_commit(&m); break;
        }
    }
}
