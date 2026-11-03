/* unit-fs: exercise the IPC hardening in the real fs write path.
 * We include the production source (not a copy) and link against the
 * host 32-bit libc now that gcc-multilib is present, so the guards
 * under test are the very ones the service runs. */

#include <stdio.h>
#include <string.h>

#include "servers/fs/fsrw.c"

/* Stand-ins for the service's kernel-facing helpers. */
struct superblock sb;
struct inode inodes[NINODES];
static int reply_arg0, refuse_called;
static char refuse_reason[64];

int ata_read(u32 lba, void *buf) { (void)lba; memset(buf, 0, SECTOR); return 0; }
int ata_write(u32 lba, const void *buf) { (void)lba; (void)buf; return 0; }

void reply(struct msg *q, int a0, int a1, int a2, const char *d, int dl) {
    (void)q; (void)a1; (void)a2; (void)d; (void)dl; reply_arg0 = a0;
}
void refuse(struct msg *q, const char *why) {
    (void)q; refuse_called = 1;
    strncpy(refuse_reason, why, sizeof refuse_reason - 1);
    refuse_reason[sizeof refuse_reason - 1] = 0;
}

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    memset(inodes, 0, sizeof inodes);
    sb.magic = FS_MAGIC; sb.n_inodes = NINODES;
    sb.data_start = DATA_START; sb.n_data = 1024;

    struct msg m; memset(&m, 0, sizeof m);

    /* bad inode index must be refused, not dereferenced */
    m.type = MSG_FS_WRITE; m.arg0 = 999; m.arg1 = 0; m.arg2 = 4;
    refuse_called = 0; do_write(&m);
    CHECK(refuse_called && strstr(refuse_reason, "bad inode"),
          "bad inode index refused");

    /* negative length clamps to zero and claims no block */
    inodes[0].used = 1; memset(inodes[0].direct, 0, sizeof inodes[0].direct);
    m.type = MSG_FS_WRITE; m.arg0 = 0; m.arg1 = 0; m.arg2 = -5;
    refuse_called = 0; reply_arg0 = -99; do_write(&m);
    CHECK(reply_arg0 == 0, "negative length replies zero");
    CHECK(inodes[0].direct[0] == 0, "negative length claims no block");

    /* zero length must not claim a block either */
    m.arg2 = 0; reply_arg0 = -99; do_write(&m);
    CHECK(reply_arg0 == 0, "zero length replies zero");
    CHECK(inodes[0].direct[0] == 0, "zero length claims no block");

    /* a real write still claims and reports */
    m.arg2 = 4; reply_arg0 = -99; do_write(&m);
    CHECK(reply_arg0 == 4, "valid write replies length");
    CHECK(inodes[0].direct[0] == DATA_START, "valid write claims a block");

    /* commit size above MAXFILE refused */
    m.type = MSG_FS_COMMIT; m.arg0 = 0; m.arg1 = MAXFILE + 1;
    refuse_called = 0; do_commit(&m);
    CHECK(refuse_called && strstr(refuse_reason, "bad size"),
          "oversize commit refused");

    /* commit within bounds succeeds */
    m.arg1 = 100; refuse_called = 0; reply_arg0 = -99; do_commit(&m);
    CHECK(!refuse_called && reply_arg0 == 0 && inodes[0].size == 100,
          "valid commit accepted");

    if (fails) { printf("unit-fs: %d failure(s)\n", fails); return 1; }
    printf("unit-fs: all ok\n");
    return 0;
}
