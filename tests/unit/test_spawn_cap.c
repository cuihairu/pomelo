/* unit-spawn-cap: the cmd_spawn line-buffer bound the November audit
 * demanded. spawn_name_cap lives in apps/shell/shell.h as a pure inline,
 * so this test includes that header directly and checks the rule the
 * shell itself runs -- no production translation unit needed.
 *
 * The bug: `spawn fs<long garbage>` was accepted (the kernel matches a
 * program by its first PROG_NAME bytes, so the name resolves and tid>=0),
 * then cmd_spawn copied the whole tty line into line[48] and the fixed
 * suffix after it, writing past the stack buffer. The cap must leave
 * SPAWN_SUFFIX bytes for " is task <n> now\n" and the NUL. */

#include <stdio.h>

#include "apps/shell/shell.h"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* a 48-byte buffer holds the suffix (16) and 32 name bytes */
    CHECK(spawn_name_cap(48) == 32, "48-byte line leaves 32 name bytes");

    /* a short name fits untouched: the cap never bites */
    CHECK(spawn_name_cap(48) >= 2, "a 2-char name fits in the 48-byte line");
    CHECK(spawn_name_cap(48) >= 8, "an 8-char name fits in the 48-byte line");

    /* the boundary: a buffer that cannot fit the suffix has no room at all */
    CHECK(spawn_name_cap(16) == 0, "suffix-only buffer leaves no name room");
    CHECK(spawn_name_cap(10) == 0, "too-small buffer leaves no name room");
    CHECK(spawn_name_cap(0) == 0, "zero buffer leaves no name room");

    /* the exact-fit line: name fills to the cap, suffix still fits */
    CHECK(spawn_name_cap(32) == 16, "32-byte line leaves 16 name bytes");
    CHECK(spawn_name_cap(17) == 1, "17-byte line leaves exactly 1 name byte");

    if (fails) { printf("unit-spawn-cap: %d failure(s)\n", fails); return 1; }
    printf("unit-spawn-cap: all ok\n");
    return 0;
}
