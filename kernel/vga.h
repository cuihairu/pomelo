#ifndef POMELO_VGA_H
#define POMELO_VGA_H

/* VGA text mode: 80x25 cells at 0xB8000, 2 bytes each (char + color).
 * Both kprintf (kernel console) and servers/tty write here. */
void vga_putc(char c);
void vga_clear(void);

#endif
