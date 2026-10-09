#include "../../kernel/string.h"
#include "../../kernel/vga.h"
#include "../../kernel/intr/intr.h"
#include "../../kernel/syscall/syscall.h"
#include "tty.h"

/* tty.c: the line-discipline layer. Chars arrive one IRQ at a time; the
 * shell wants whole lines. We edit, echo, and only then hand over.
 * Completed lines queue up until the shell asks for them: a reader that
 * lags behind must never cost the typist their keystrokes -- the same
 * rule a real terminal's canonical mode lives by. */

#define DONE_CAP 4              /* completed lines awaiting a reader */

static char line[MSG_DATA];
static int  len;
static struct { char buf[DONE_CAP][MSG_DATA]; int len[DONE_CAP]; } done;
static int done_head, done_cnt; /* ring of unclaimed lines */
static struct msg pending;      /* a getline request waiting for a line */
static int have_pending;

static void deliver(void) {
    if (!have_pending || !done_cnt) return;
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_TTY_GETLINE;
    r.arg0 = done.len[done_head];
    memcpy(r.data, done.buf[done_head], done.len[done_head]);
    sys_send(pending.src, &r);
    have_pending = 0;
    done_head = (done_head + 1) % DONE_CAP;
    done_cnt--;
}

static void handle_char(char c) {
    if (c == '\b') {
        if (len) {
            len--;
            tty_putc('\b'); tty_putc(' '); tty_putc('\b');
        }
    } else if (c == '\n') {
        tty_putc('\n');
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
        tty_putc(c);                /* echo: the screen shows what we choose */
    }
}

/* Pick up requests sitting in the inbox; we are woken for those too. */
static void pump(void) {
    struct msg m;
    while (sys_try_recv(&m) == 0) {
        if (m.type == MSG_TTY_PUTS)
            for (int i = 0; i < m.arg0; i++) tty_putc(m.data[i]);
        else if (m.type == MSG_TTY_CLEAR)
            vga_clear();
        else if (m.type == MSG_TTY_GETLINE) {
            pending = m;
            have_pending = 1;
            deliver();
        }
    }
}

void tty_main(void) {
    console_init();
    for (;;) {
        int c;                      /* drain whatever arrived, first */
        while ((c = kbd_getc()) >= 0)
            handle_char((char)c);
        pump();                     /* then handle service requests */
        deliver();
        sys_irq_wait(IRQ_KBD | IRQ_COM1);   /* then sleep for input */
    }
}
