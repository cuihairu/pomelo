#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "tty.h"

/* The kernel starts this program like any other: the blob's first four
 * bytes are the entry point (see tools/user.ld). */
void tty_main(void);
__attribute__((section(".hdr"))) const unsigned tty_entry = (unsigned)tty_main;

/* tty.c: the line-discipline layer, now a pure service. Chars arrive as
 * messages from the kernel (which owns the keyboard and COM1); output
 * leaves through one syscall. Completed lines queue up until the shell
 * asks for them: a reader that lags behind must never cost the typist
 * their keystrokes -- the same rule a real terminal's canonical mode
 * lives by. */

#define DONE_CAP 4              /* completed lines awaiting a reader */

static char line[MSG_DATA];
static int  len;
static struct { char buf[DONE_CAP][MSG_DATA]; int len[DONE_CAP]; } done;
static int done_head, done_cnt; /* ring of unclaimed lines */
static struct msg pending;      /* a getline request waiting for a line */
static int have_pending;

static void put(char c) { sys_write(&c, 1); }

static void deliver(void) {
    if (!have_pending || !done_cnt) return;
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_TTY_GETLINE;
    r.arg0 = done.len[done_head];
    memcpy(r.data, done.buf[done_head], done.len[done_head]);
    sys_send_wait(pending.src, &r);       /* the shell must get its line */
    have_pending = 0;
    done_head = (done_head + 1) % DONE_CAP;
    done_cnt--;
}

static void handle_char(char c) {
    if (c == '\b') {
        if (len) {
            len--;
            put('\b'); put(' '); put('\b');
        }
    } else if (c == '\n') {
        put('\n');
        if (done_cnt < DONE_CAP) {      /* claim it or drop the newline */
            int tail = (done_head + done_cnt) % DONE_CAP;
            done.len[tail] = len;
            memcpy(done.buf[tail], line, len);
            done_cnt++;
        }
        len = 0;
        deliver();
    } else if ((u8)c >= 32 && len < (int)sizeof(line) - 1) {
        line[len++] = c;
        put(c);                         /* echo: the screen shows what we choose */
    }
}

static void handle(struct msg *m) {
    if (m->type == MSG_TTY_CHAR)
        handle_char(m->data[0]);
    else if (m->type == MSG_TTY_PUTS)
        sys_write(m->data, m->arg0);
    else if (m->type == MSG_TTY_CLEAR)
        sys_clear();
    else if (m->type == MSG_TTY_GETLINE) {
        pending = *m;
        have_pending = 1;
        deliver();
    }
    /* MSG_TTY_MOUSE lands here and is dropped, on purpose: the shell
     * speaks in lines, not coordinates, so today there is nothing to
     * forward it to. The call is the tty's to make, and whoever wants
     * motion data asks the same way -- the message already carries dx,
     * dy and the buttons. The on-screen block is drawn kernel-side and
     * needs no help from us. */
}

void tty_main(void) {
    struct msg m;
    for (;;) {
        while (sys_try_recv(&m) == 0)   /* drain first: chars typed while busy */
            handle(&m);
        deliver();
        sys_recv(&m);                   /* nothing left: sleep for the kernel */
        handle(&m);
    }
}
