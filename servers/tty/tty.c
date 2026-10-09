#include "../../kernel/string.h"
#include "../../kernel/vga.h"
#include "../../kernel/intr/intr.h"
#include "../../kernel/syscall/syscall.h"
#include "tty.h"

/* tty.c: the line-discipline layer. Chars arrive one IRQ at a time; the
 * shell wants whole lines. We edit, echo, and only then hand over. */

static char line[MSG_DATA];
static int  len, line_ready;
static struct msg pending;          /* a getline request waiting for a line */
static int  have_pending;

static void deliver(void) {
    if (!have_pending || !line_ready) return;
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_TTY_GETLINE;
    r.arg0 = len;
    memcpy(r.data, line, len);
    sys_send(pending.src, &r);
    have_pending = 0;
    line_ready = 0;
    len = 0;
}

static void handle_char(char c) {
    if (line_ready) return;     /* one unclaimed line: ignore new input */
    if (c == '\b') {
        if (len) {
            len--;
            tty_putc('\b'); tty_putc(' '); tty_putc('\b');
        }
    } else if (c == '\n') {
        tty_putc('\n');
        line[len] = 0;
        line_ready = 1;
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
