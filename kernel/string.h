#ifndef POMELO_STRING_H
#define POMELO_STRING_H

#ifdef __POMELO_HOST_UNIT__
/* Host unit tests include production sources and use libc's versions;
 * redeclaring them with the same signatures triggers GCC's prototype
 * diagnostics even when the ABI is identical. */
#include <string.h>
#else

/* Freestanding kernels must provide their own libc basics: gcc is free
 * to emit calls to these even with -fno-builtin. */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
void *memmove(void *d, const void *s, unsigned n);
int   strncmp(const char *a, const char *b, unsigned n);
int   memcmp(const void *a, const void *b, unsigned n);
unsigned strnlen(const char *s, unsigned max);

#endif
#endif
