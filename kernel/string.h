#ifndef POMELO_STRING_H
#define POMELO_STRING_H

/* Freestanding kernels must provide their own libc basics: gcc is free
 * to emit calls to these even with -fno-builtin. */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int   strncmp(const char *a, const char *b, unsigned n);
unsigned strnlen(const char *s, unsigned max);

#endif
