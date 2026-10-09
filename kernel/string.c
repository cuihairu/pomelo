#include "types.h"
#include "string.h"

void *memset(void *d, int c, unsigned n) {
    u8 *p = d;
    while (n--) *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n) {
    u8 *dp = d;
    const u8 *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

int strncmp(const char *a, const char *b, unsigned n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (u8)*a - (u8)*b;
        if (!*a) return 0;
    }
    return 0;
}

unsigned strnlen(const char *s, unsigned max) {
    unsigned n = 0;
    while (n < max && s[n]) n++;
    return n;
}
