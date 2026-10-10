#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "../../apps/con.h"
#include "format.h"
#include "fs.h"

/* The kernel starts this program like any other: the blob's first four
 * bytes are the entry point (see tools/user.ld). */
void fs_main(void);
__attribute__((section(".hdr"))) const unsigned fs_entry = (unsigned)fs_main;

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
    sys_send_wait(q->src, &r);            /* a lost reply strands the client */
}

void do_read(struct msg *m);      /* fsrw.c */
void do_create(struct msg *m);
void do_write(struct msg *m);
void do_commit(struct msg *m);

/* A service that cannot serve still owes its client an answer: one
 * MSG_FS_ERR with the reason in the data. */
void refuse(struct msg *q, const char *why) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_FS_ERR;
    memcpy(r.data, why, strnlen(why, MSG_DATA - 1));
    sys_send_wait(q->src, &r);            /* "no" must arrive too */
}

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
    struct msg m;
    const char *why = 0;          /* why the disk is unusable, if it is */
    if (ata_read(0, &sb) < 0) {
        con_puts("fs: no disk behind the ata ports, answering no\n");
        why = "no disk";
    } else if (sb.magic != FS_MAGIC) {
        con_puts("fs: bad disk magic, answering no\n");
        why = "bad magic";
    } else {
        for (int s = 0; s < INODE_SECTORS; s++)
            ata_read(INODE_START + s, (u8 *)inodes + s * SECTOR);
    }

    for (;;) {
        sys_recv(&m);
        if (why) { refuse(&m, why); continue; }   /* cannot serve: say so */
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
