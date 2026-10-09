#ifndef POMELO_TTY_H
#define POMELO_TTY_H

/* tty <-> shell protocol. The shell prints and reads lines only through
 * these messages; it never touches the screen or the keyboard itself. */
#define MSG_TTY_PUTS    1   /* arg0=len, data: print exactly len bytes */
#define MSG_TTY_GETLINE 2   /* reply: arg0=len, data = the completed line */
#define MSG_TTY_CLEAR   3

void tty_main(void);

/* console.c */
void console_init(void);
void tty_putc(char c);

/* kbd.c: one character ring, fed by the keyboard and by com1 */
void kbd_isr(void);
void kbd_push(char c);
int  kbd_getc(void);

#endif
