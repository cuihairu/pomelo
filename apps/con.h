#ifndef POMELO_CON_H
#define POMELO_CON_H

/* User-side console helpers: output goes through the kernel, one syscall
 * for a run of characters. Services never touch ports or VGA memory. */

#include "../kernel/string.h"
#include "../kernel/syscall/syscall.h"

static inline void con_write(const char *s, int n) { sys_write(s, n); }
static inline void con_clear(void)                  { sys_clear(); }
static inline void con_puts(const char *s)          { con_write(s, strnlen(s, 512)); }

#endif
