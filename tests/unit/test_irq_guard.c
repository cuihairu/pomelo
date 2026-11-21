/* unit-irq-guard: the SYS_IRQ_ENABLE / SYS_IRQ_EOI gate the November
 * audit demanded. irq_no_ok and irq_kernel live in intr/intr.h as pure
 * inlines, so this test includes that header directly and checks the
 * predicates the kernel itself runs -- no production translation unit
 * needed.
 *
 * The rule has two halves: only the two 8259s' sixteen lines exist, and
 * some of those sixteen belong to the kernel forever. The old guard
 * accepted irq 0 and irq 1 -- which let any task claim the clock line
 * and strand its eoi; irq_kernel is the half that refuses them. */

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
    CHECK(irq_no_ok(0), "irq 0 in range");
    CHECK(irq_no_ok(1), "irq 1 in range");
    CHECK(irq_no_ok(4), "irq 4 in range");
    CHECK(irq_no_ok(12), "irq 12 in range");
    CHECK(irq_no_ok(15), "irq 15 in range");
    CHECK(!irq_no_ok(16), "irq 16 rejected");
    CHECK(!irq_no_ok(255), "irq 255 rejected");
    CHECK(!irq_no_ok(0xFFFFFFFFu), "irq -1 rejected");

    /* the kernel's own lines: the timer, the keyboard, the cascade, the
     * serial port, the mouse. A claim on any of them would strand the
     * eoi (kernel isrs cannot reach sys_irq_eoi), so irq_claim refuses
     * exactly what irq_kernel reports. */
    CHECK(irq_kernel(IRQ_PIT), "irq 0 is the kernel's clock");
    CHECK(irq_kernel(IRQ_KBD), "irq 1 is the kernel's keyboard");
    CHECK(irq_kernel(2), "irq 2 is the kernel's cascade");
    CHECK(irq_kernel(IRQ_COM1), "irq 4 is the kernel's serial");
    CHECK(irq_kernel(IRQ_MOUSE), "irq 12 is the kernel's mouse");

    /* every other line is a driver's to claim */
    CHECK(!irq_kernel(3), "irq 3 is claimable");
    CHECK(!irq_kernel(5), "irq 5 is claimable");
    CHECK(!irq_kernel(9), "irq 9 is claimable");
    CHECK(!irq_kernel(11), "irq 11 is claimable");
    CHECK(!irq_kernel(13), "irq 13 is claimable");
    CHECK(!irq_kernel(15), "irq 15 is claimable");
    for (u32 i = 0; i < NIRQ; i++)
        if (!irq_kernel(i)) {
            char msg[40];
            sprintf(msg, "claimable line %u is in range", i);
            CHECK(irq_no_ok(i), msg);
        }

    /* out-of-range numbers are nobody's, not the kernel's -- the range
     * check runs first in the gate, and irq_kernel never shifts past
     * the mask either. */
    CHECK(!irq_kernel(16), "irq 16 is not a kernel line, it is nothing");
    CHECK(!irq_kernel(0xFFFFFFFFu), "irq -1 is not a kernel line");

    if (fails) { printf("unit-irq-guard: %d failure(s)\n", fails); return 1; }
    printf("unit-irq-guard: all ok\n");
    return 0;
}
