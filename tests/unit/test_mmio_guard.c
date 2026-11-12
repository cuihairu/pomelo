/* unit-mmio-guard: the SYS_MMIO gate the November audit demanded.
 * mmio_pa_ok lives in mm/paging.h as a pure inline, so this test
 * includes that header directly and checks the predicate the kernel
 * itself runs -- no production translation unit needed. */

#include <stdio.h>

#include "kernel/mm/paging.h"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* RAM below the identity span never crosses into a task's space;
     * device memory above it always may. The 0x101000 the probe attack
     * used is the first must-reject. */
    CHECK(!mmio_pa_ok(0), "pa 0 rejected");
    CHECK(!mmio_pa_ok(0x101000), "probe's 0x101000 rejected");
    CHECK(!mmio_pa_ok(SPAN - 1), "last RAM page rejected");
    CHECK(!mmio_pa_ok(0x400000), "4MB rejected");
    CHECK(mmio_pa_ok(SPAN), "first device page accepted");
    CHECK(mmio_pa_ok(SPAN + 0x8000), "device page accepted");
    CHECK(mmio_pa_ok(0xF0000000), "top-of-space accepted");

    if (fails) { printf("unit-mmio-guard: %d failure(s)\n", fails); return 1; }
    printf("unit-mmio-guard: all ok\n");
    return 0;
}
