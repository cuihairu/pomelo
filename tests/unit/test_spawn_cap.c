/* unit-spawn-cap: the cmd_spawn line-buffer bound, and the formatting that
 * enforces it.
 *
 * The honest story first. An earlier ledger entry said a long name both
 * resolved and overran the buffer: `spawn fs<long garbage>`. The first half
 * was wrong -- the kernel matches a program with strncmp against PROG_NAME,
 * so the name mismatches at its own NUL, the spawn never happens and
 * cmd_spawn returns before it copies anything. What is real is that argv[1]
 * is a tty line and therefore untrusted in length while line[] is 48 bytes,
 * so any caller path that does reach the copy must be clamped there. The cap
 * and the formatter are that defensive bound; this test drives the formatter
 * itself, so a regression in the loop, in the suffix or in either bound is
 * visible here instead of only in an arithmetic identity. */

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

    /* And the path cmd_spawn really runs, not just the arithmetic above.
     * A 60-byte name against a 48-byte buffer, with a sentinel four bytes
     * past the end of line: if the copy ran long it would land here. */
    char buf[52];
    for (int i = 0; i < 48; i++) buf[i] = (char)0x11;
    for (int i = 48; i < 52; i++) buf[i] = (char)0xAA;
    char name[64];
    for (int i = 0; i < 60; i++) name[i] = 'x';
    name[60] = 0;

    int p = spawn_line(buf, 48, name, 3);
    CHECK(p == 47, "a long name writes exactly the name cap plus suffix");
    CHECK(buf[p] == 0 && p < 48, "and the line ends inside the buffer");
    CHECK(buf[48] == (char)0xAA && buf[49] == (char)0xAA &&
          buf[50] == (char)0xAA && buf[51] == (char)0xAA,
          "nothing ran past the end of the buffer");
    CHECK(buf[31] == 'x', "the name fills to the cap");
    CHECK(buf[32] == ' ' && buf[33] == 'i' && buf[34] == 's',
          "the suffix starts where the truncated name ends");
    CHECK(buf[41] == '3', "the task number is printed");
    CHECK(buf[42] == ' ' && buf[43] == 'n' && buf[46] == '\n',
          "and the ending is intact");

    /* a name short enough to fit whole: the cap must not bite at all */
    char short_line[48];
    int p2 = spawn_line(short_line, 48, "fs", 5);
    CHECK(p2 == 17, "a short name writes name plus suffix");
    CHECK(short_line[0] == 'f' && short_line[1] == 's', "the name is whole");
    CHECK(short_line[2] == ' ' && short_line[11] == '5',
          "the suffix follows it in order");
    CHECK(short_line[16] == '\n' && short_line[17] == 0, "and it is terminated");

    if (fails) { printf("unit-spawn-cap: %d failure(s)\n", fails); return 1; }
    printf("unit-spawn-cap: all ok\n");
    return 0;
}
