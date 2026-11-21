/* unit-frame-nest: the teardown preemption the November audit demanded.
 * task_fault clears the interrupt flag, then frees every frame of a dead
 * task -- and frame_free used to end with an unconditional sti, so each
 * free reopened the flag in the middle of the death and handed the CPU to
 * a timer interrupt while the task's page directory was still being
 * torn down. The allocator now saves the flag it found and restores it.
 *
 * cli and sti are ring 0, so a host test cannot execute them; it records
 * the state instead and checks what frame_alloc and frame_free leave
 * behind. That is exactly the property the fix buys. frame_init is left
 * out on purpose: it walks from the address of kernel_end, which on a
 * host is a virtual address and not the end of a physical image, and the
 * walk would run off the end of the bitmap. Where the image ends is not
 * what is being guarded here. */

#include <stdio.h>

/* Defined before the kernel headers so kernel/types.h declares the pair
 * instead of compiling its cli/sti pair into frame.o. */
#define POMELO_INTR_TEST 1

#include "kernel/mm/frame.h"

/* The allocator's view of the interrupt flag: external linkage, so the
 * calls frame.o makes reach these and not a copy of the kernel's own. */
static int intr_state = 1;
u32 intr_save(void) {
    u32 f = intr_state ? 0x200u : 0;
    intr_state = 0;
    return f;
}
void intr_restore(u32 f) {
    intr_state = (f & 0x200u) ? 1 : 0;
}

/* frame.c also asks whether a frame a card still writes is held. */
int dma_pinned(u32 pa) { (void)pa; return 0; }

u8 kernel_end[8];                    /* frame_init's image end: unused */

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* The bitmap starts empty, so the first ask takes frame 0 at address
     * 0 -- the allocator's "none" answer -- and is skipped. */
    CHECK(frame_alloc() == 0, "the first frame reads as the none answer");
    u32 a = frame_alloc();
    u32 b = frame_alloc();
    CHECK(a && b, "the next two frames come out of the bitmap");
    CHECK(intr_state == 1, "an allocation with interrupts on leaves them on");

    /* The teardown path: the flag is already clear, and every free must
     * leave it clear -- not hand the CPU back to a timer interrupt. */
    intr_state = 0;
    frame_free(a);
    CHECK(intr_state == 0, "a free inside a teardown keeps the flag off");
    frame_free(b);
    CHECK(intr_state == 0, "the second free keeps it off too");
    u32 c = frame_alloc();
    CHECK(c == a, "the freed frame is handed back to the next ask");
    CHECK(intr_state == 0, "an allocation inside the teardown keeps it off");

    /* and the ordinary caller is unchanged: on in, on out */
    intr_state = 1;
    frame_free(c);
    CHECK(intr_state == 1, "an ordinary free leaves the flag as it found it");
    struct mem_info mi;
    frame_stats(&mi);
    CHECK(intr_state == 1, "the count leaves the flag as it found it");
    CHECK(mi.total == 4096 && mi.used == 1 && mi.free == 4095,
          "the snapshot agrees with the bitmap it counted");

    if (fails) { printf("unit-frame-nest: %d failure(s)\n", fails); return 1; }
    printf("unit-frame-nest: all ok\n");
    return 0;
}
