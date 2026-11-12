/* unit-irq-guard: the SYS_IRQ_ENABLE / SYS_IRQ_EOI gate the November
 * audit demanded. irq_no_ok lives in intr/intr.h as a pure inline, so
 * this test includes that header directly and checks the predicate the
 * kernel itself runs -- no production translation unit needed. */

#include <stdio.h>

#include "kernel/intr/intr.h"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* the sixteen pic lines exist; irq.c indexes its handler/waiter
     * tables by the line, so 16+ must be rejected everywhere -- a bogus
     * one would shift 1u << irq past the mask and poke ports that belong
     * to nobody. */
    CHECK(irq_no_ok(0), "irq 0 accepted");
    CHECK(irq_no_ok(1), "irq 1 accepted");
    CHECK(irq_no_ok(4), "irq 4 accepted");
    CHECK(irq_no_ok(12), "irq 12 accepted");
    CHECK(irq_no_ok(15), "irq 15 accepted");
    CHECK(!irq_no_ok(16), "irq 16 rejected");
    CHECK(!irq_no_ok(255), "irq 255 rejected");
    CHECK(!irq_no_ok(0xFFFFFFFFu), "irq -1 rejected");

    if (fails) { printf("unit-irq-guard: %d failure(s)\n", fails); return 1; }
    printf("unit-irq-guard: all ok\n");
    return 0;
}
