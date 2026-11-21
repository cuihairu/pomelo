#include "dma.h"
#include "frame.h"

/* See dma.h for why the kernel keeps a driver's DMA frames allocated
 * past the driver's death. */

static u32 pins[NIRQ][NPIN];     /* physical frame bases; 0 = empty slot */
static u8  used[NIRQ];           /* how many slots each line holds */

void dma_pin(u32 line, u32 pa) {
    u32 base = pa & ~0xFFFu;
    if (line >= NIRQ || !base) return;
    for (u8 i = 0; i < NPIN; i++)
        if (pins[line][i] == base) return;      /* two descriptors, one frame */
    if (used[line] == NPIN) return;             /* window full; see dma.h */
    pins[line][used[line]++] = base;
}

int dma_pinned(u32 pa) {
    u32 base = pa & ~0xFFFu;
    for (u32 l = 0; l < NIRQ; l++)
        for (u8 i = 0; i < NPIN; i++)
            if (pins[l][i] == base) return 1;
    return 0;
}

void dma_release(u32 line) {
    if (line >= NIRQ) return;
    for (u8 i = 0; i < used[line]; i++) {
        u32 base = pins[line][i];
        pins[line][i] = 0;      /* drop the stamp first, or the
                                   allocator's own guard would eat the
                                   free and the frame would stay held */
        frame_free(base);
    }
    used[line] = 0;
}
