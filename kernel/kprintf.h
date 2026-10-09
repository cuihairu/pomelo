#ifndef POMELO_KPRINTF_H
#define POMELO_KPRINTF_H

/* Formatting supports %s %c %d %u %x and %%. Anything that must reach a
 * user task goes through servers/tty instead; this is kernel-side only. */
void kprintf(const char *fmt, ...);

#endif
