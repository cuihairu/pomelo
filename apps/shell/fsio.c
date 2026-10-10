#include "../../kernel/string.h"
#include "shell.h"

/* Client wrappers for the fs protocol: build a request, send it to
 * TID_FS, block for the reply. Tiny, and each maps to one message pair.
 * Every send is sys_send_wait: a request the queue refused would leave
 * this task waiting for a reply that is never coming.
 *
 * Returns: the answer, or FS_REFUSED (shell.h) when the server said
 * no -- the reason is already on the screen by then, so callers keep
 * quiet. */

/* Every reply may be a refusal: MSG_FS_ERR prints its reason, returns -1. */
static int reply_wait(struct msg *r) {
    sys_recv(r);
    if (r->type != MSG_FS_ERR) return 0;
    tty_puts("fs: ");
    tty_write(r->data, strnlen(r->data, MSG_DATA));
    tty_puts("\n");
    return -1;
}

int fs_open(const char *name, int *size) {
    struct msg m, r;
    memset(&m, 0, sizeof m);
    m.type = MSG_FS_OPEN;
    memcpy(m.data, name, strnlen(name, MSG_DATA - 1));
    sys_send_wait(TID_FS, &m);
    if (reply_wait(&r) < 0) return FS_REFUSED;
    if (size) *size = r.arg1;
    return r.arg0;
}

int fs_read(int ino, int off, char *buf) {
    struct msg m, r;
    memset(&m, 0, sizeof m);
    m.type = MSG_FS_READ;
    m.arg0 = ino;
    m.arg1 = off;
    sys_send_wait(TID_FS, &m);
    if (reply_wait(&r) < 0) return FS_REFUSED;
    memcpy(buf, r.data, r.arg0);
    return r.arg0;
}

int fs_create(const char *name) {
    struct msg m, r;
    memset(&m, 0, sizeof m);
    m.type = MSG_FS_CREATE;
    memcpy(m.data, name, strnlen(name, MSG_DATA - 1));
    sys_send_wait(TID_FS, &m);
    if (reply_wait(&r) < 0) return FS_REFUSED;
    return r.arg0;
}

int fs_write(int ino, int off, const char *buf, int n) {
    struct msg m, r;
    if (n > MSG_DATA) n = MSG_DATA;
    memset(&m, 0, sizeof m);
    m.type = MSG_FS_WRITE;
    m.arg0 = ino;
    m.arg1 = off;
    m.arg2 = n;
    memcpy(m.data, buf, n);
    sys_send_wait(TID_FS, &m);
    if (reply_wait(&r) < 0) return FS_REFUSED;
    return r.arg0;
}

int fs_commit(int ino, int size) {
    struct msg m, r;
    memset(&m, 0, sizeof m);
    m.type = MSG_FS_COMMIT;
    m.arg0 = ino;
    m.arg1 = size;
    sys_send_wait(TID_FS, &m);
    if (reply_wait(&r) < 0) return FS_REFUSED;
    return r.arg0;
}

void fs_ls_begin(void) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_FS_LS;
    sys_send_wait(TID_FS, &m);
}

int fs_ls_next(char *name, int *size) {
    struct msg r;
    if (reply_wait(&r) < 0) return 1;
    if (r.arg0 < 0) return 1;
    memcpy(name, r.data, NAMELEN);
    name[NAMELEN] = 0;
    *size = r.arg0;
    return 0;
}

void num_str(int v, char *buf) {
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = '0' + v % 10; v /= 10; }
    while (i--) buf[j++] = tmp[i];
    buf[j] = 0;
}
