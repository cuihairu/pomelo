#ifndef POMELO_MOUSE_H
#define POMELO_MOUSE_H

/* The kernel owns the PS/2 mouse the way it owns the keyboard: the isr
 * lives here, motion becomes a message for tty (MSG_TTY_MOUSE) and the
 * screen block is drawn by vga.c. Services never touch the controller. */

void mouse_init(void);                      /* probe, enable, wire irq12 */

#endif
