#ifndef POMELO_TTY_H
#define POMELO_TTY_H

#include "../../kernel/ipc/ipc.h"   /* MSG_DATA: the payload field's size */

/* tty protocol. The shell prints and reads lines only through these
 * messages; it never touches the screen or the keyboard itself. Input
 * reaches us as MSG_TTY_CHAR -- one per keystroke -- because the
 * console hardware belongs to the kernel (kernel/char.c). */
#define MSG_TTY_PUTS    1   /* arg0=len, data: print exactly len bytes */
#define MSG_TTY_GETLINE 2   /* reply: arg0=len, data = the completed line */
#define MSG_TTY_CLEAR   3
#define MSG_TTY_CHAR    4   /* from the kernel: one raw input character */
#define MSG_TTY_MOUSE   5   /* from the kernel: arg0=dx, arg1=dy, arg2=buttons */

/* A puts length crossed the tty's trust boundary: an untrusted sender
 * may claim anything. The payload the kernel delivered is at most
 * MSG_DATA bytes, so clamp -- a bogus length must never reach
 * sys_write, which would print the tty's own stack (the bytes past
 * data[] belong to this very frame). Pure, so the host unit test
 * shares the service's exact rule. */
static inline int tty_puts_len(int arg0) {
    if (arg0 < 0) return 0;
    if (arg0 > MSG_DATA) return MSG_DATA;
    return arg0;
}

#endif
