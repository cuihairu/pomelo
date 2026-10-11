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

/* memcpy that also works when the ends overlap: copy backwards when the
 * destination sits above the source, so the bytes still to be read are
 * not stepped on. The socket buffer's compaction is the caller that
 * needs this. */
void *memmove(void *d, const void *s, unsigned n) {
    u8 *dp = d;
    const u8 *sp = s;
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--) *--dp = *--sp;
    }
    return d;
}

int strncmp(const char *a, const char *b, unsigned n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (u8)*a - (u8)*b;
        if (!*a) return 0;
    }
    return 0;
}

int memcmp(const void *a, const void *b, unsigned n) {
    const u8 *pa = a;
    const u8 *pb = b;
    for (; n; n--, pa++, pb++)
        if (*pa != *pb) return *pa - *pb;
    return 0;
}

unsigned strnlen(const char *s, unsigned max) {
    unsigned n = 0;
    while (n < max && s[n]) n++;
    return n;
}
