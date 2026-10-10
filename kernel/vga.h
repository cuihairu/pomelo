#ifndef POMELO_VGA_H
#define POMELO_VGA_H

/* VGA text mode: 80x25 cells at 0xB8000, 2 bytes each (char + color).
 * Both kprintf (kernel console) and servers/tty write here. The mouse
 * cursor is a block drawn by the same hand, because text and block share
 * one buffer and only the kernel may touch it. */
void vga_putc(char c);
void vga_clear(void);
void vga_mouse_home(void);              /* park the block mid-screen */
void vga_mouse_move(int dx, int dy);    /* step it, clamped to the screen */

#endif
