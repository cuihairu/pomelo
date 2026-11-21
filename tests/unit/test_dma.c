/* unit-dma: the frames a driver hands to a card must survive the
 * driver's death. task_fault frees every frame of a dying task while the
 * card keeps writing into the ones the driver converted through
 * sys_v2p, so the kernel stamps them here and dma_release gives them
 * back only when the line is claimed again -- the new bringup has reset
 * the card by then, so that DMA is over.
 *
 * This test links the real dma.c and records every frame_free it asks
 * for, so the stamping, the dedup, the cap and the hand-back are all
 * checked against what the allocator would actually see. */

#include <stdio.h>

#include "kernel/mm/dma.h"

/* The allocator's view: every frame dma_release asks back. */
static u32 freed[64];
static int fn;
void frame_free(u32 pa) { if (fn < 64) freed[fn++] = pa; }

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

static void reset(void) {
    for (int i = 0; i < 64; i++) freed[i] = 0;
    fn = 0;
    dma_release(0); dma_release(5); dma_release(6);
}

int main(void) {
    reset();

    /* two descriptors in one frame stamp it once: the dedup is by frame
     * base, so an offset inside the frame cannot make two entries */
    dma_pin(5, 0x100000);
    dma_pin(5, 0x100FF0);
    CHECK(dma_pinned(0x100000), "the stamped frame is pinned");
    CHECK(dma_pinned(0x100FF0), "any address inside it reads as pinned");
    CHECK(!dma_pinned(0x101000), "the next frame is not pinned");
    dma_release(5);
    CHECK(fn == 1 && freed[0] == 0x100000, "the frame is handed back once");
    CHECK(!dma_pinned(0x100000), "release clears the stamp");

    /* releasing an empty line frees nothing */
    reset();
    dma_release(5);
    CHECK(fn == 0, "releasing a line with no pins frees nothing");

    /* pins belong to the line that stamped them */
    dma_pin(5, 0x100000);
    dma_pin(6, 0x110000);
    dma_release(5);
    CHECK(fn == 1 && freed[0] == 0x100000, "releasing line 5 frees only its own frame");
    CHECK(dma_pinned(0x110000), "line 6's frame stays held");
    dma_release(6);
    CHECK(fn == 2 && freed[1] == 0x110000, "and line 6's comes back when it is claimed");

    /* the cap: NPIN entries are what the kernel promises to remember */
    reset();
    for (int i = 0; i < 20; i++)
        dma_pin(6, 0x100000 + (u32)i * 4096);
    dma_release(6);
    CHECK(fn == NPIN, "the window holds NPIN frames and no more");
    CHECK(!dma_pinned(0x100000 + (u32)19 * 4096),
          "a frame past the window was never stamped");

    /* a bogus line is ignored, not an index past the table */
    reset();
    dma_pin(16, 0x100000);
    dma_release(16);
    CHECK(fn == 0, "a line outside the pic is ignored");

    if (fails) { printf("unit-dma: %d failure(s)\n", fails); return 1; }
    printf("unit-dma: all ok\n");
    return 0;
}
