/* unit-tty-puts: the MSG_TTY_PUTS length clamp the November audit
 * demanded. tty_puts_len lives in servers/tty/tty.h as a pure inline,
 * so this test includes that header directly and checks the rule the
 * service itself runs -- no production translation unit needed. */

#include <stdio.h>

#include "servers/tty/tty.h"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* a puts length from an untrusted sender may claim anything; the
     * payload the kernel delivered is at most MSG_DATA bytes, so the
     * clamp must hold it in [0, MSG_DATA] -- a 512 claim would print
     * the tty's own stack if it ever reached sys_write. */
    CHECK(tty_puts_len(-5) == 0, "negative clamps to 0");
    CHECK(tty_puts_len(0) == 0, "zero stays 0");
    CHECK(tty_puts_len(1) == 1, "one stays 1");
    CHECK(tty_puts_len(MSG_DATA) == MSG_DATA, "exact payload stays");
    CHECK(tty_puts_len(MSG_DATA + 1) == MSG_DATA, "one past clamps to payload");
    CHECK(tty_puts_len(512) == MSG_DATA, "512 clamps to payload");
    CHECK(tty_puts_len(4095) == MSG_DATA, "4095 clamps to payload");

    if (fails) { printf("unit-tty-puts: %d failure(s)\n", fails); return 1; }
    printf("unit-tty-puts: all ok\n");
    return 0;
}
