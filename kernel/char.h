#ifndef POMELO_CHAR_H
#define POMELO_CHAR_H

/* The kernel owns the console hardware: COM1 and the PS/2 keyboard on
 * the way in, VGA plus COM1 on the way out. Services see characters as
 * messages (in) and one syscall (out), never ports. */

#define CON_COM1 0x3F8

void char_init(void);                       /* wire the input ISRs */
void char_retry(void);                      /* timer: retry held input */
void con_putc(char c);                      /* vga + serial, kernel side */
void con_write(const char *s, int n);
void con_clear(void);                       /* vga_clear */

#endif
