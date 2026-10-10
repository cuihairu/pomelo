#ifndef POMELO_TTY_H
#define POMELO_TTY_H

/* tty protocol. The shell prints and reads lines only through these
 * messages; it never touches the screen or the keyboard itself. Input
 * reaches us as MSG_TTY_CHAR -- one per keystroke -- because the
 * console hardware belongs to the kernel (kernel/char.c). */
#define MSG_TTY_PUTS    1   /* arg0=len, data: print exactly len bytes */
#define MSG_TTY_GETLINE 2   /* reply: arg0=len, data = the completed line */
#define MSG_TTY_CLEAR   3
#define MSG_TTY_CHAR    4   /* from the kernel: one raw input character */
#define MSG_TTY_MOUSE   5   /* from the kernel: arg0=dx, arg1=dy, arg2=buttons */

#endif
