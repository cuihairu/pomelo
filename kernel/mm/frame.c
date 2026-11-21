#include "frame.h"
#include "dma.h"
#include "../intr/intr.h"

/* The frame allocator is one bitmap, as the roadmap promised. Everything
 * below the end of the kernel image is marked used up front: BIOS data,
 * VGA text, the boot stack and the kernel's own .bss all live down there,
 * and none of it may ever be handed out. Above that line, up to FRAME_TOP,
 * frames are free for page tables and user pages. */

#define FRAME_TOP (16u << 20)            /* 16 MB of physical memory */
#define NFRAMES   (FRAME_TOP / 4096)

static u8 used[NFRAMES / 8];
static u32 first_free;                   /* low-water mark, frames below are gone */

extern u8 kernel_end[];                  /* boot/kernel.ld */

static void mark(u32 f)      { used[f >> 3] |= (u8)(1 << (f & 7)); }
static void unmark(u32 f)    { used[f >> 3] &= (u8)~(1 << (f & 7)); }
static int  is_used(u32 f)   { return used[f >> 3] & (1 << (f & 7)); }

void frame_init(void) {
    first_free = (((u32)kernel_end + 4095) & ~4095u) / 4096;
    for (u32 f = 0; f < first_free; f++) mark(f);
}

u32 frame_alloc(void) {
    /* Save and restore rather than disable and enable: a tick could hand
     * one frame to two spawners, and frame_alloc is also called from the
     * middle of a teardown that must stay unpreemptible (intr_save keeps
     * whatever state it found). */
    u32 flags = intr_save();
    for (u32 f = first_free; f < NFRAMES; f++) {
        if (!is_used(f)) {
            mark(f);
            if (f == first_free) first_free++;
            intr_restore(flags);
            return f * 4096;
        }
    }
    intr_restore(flags);
    return 0;
}

void frame_free(u32 pa) {
    /* A dying task's DMA frames are returned by pdir_free too, and the
     * card behind them is still writing into them -- releasing one would
     * hand the very page to the next frame_alloc while the card repaints
     * it. dma_release gives them back once the line is re-claimed. */
    if (dma_pinned(pa)) return;
    u32 flags = intr_save();
    unmark(pa / 4096);
    if (pa / 4096 < first_free) first_free = pa / 4096;
    intr_restore(flags);
}

/* Count the bitmap for SYS_MEM. Interrupts off so total/used/free agree
 * with each other -- a tick in the middle could hand out a frame between
 * two counts and make the numbers not add up. */
void frame_stats(struct mem_info *out) {
    u32 flags = intr_save();
    u32 used = 0;
    for (u32 f = 0; f < NFRAMES; f++)
        if (is_used(f)) used++;
    out->total = NFRAMES;
    out->used = used;
    out->free = NFRAMES - used;
    intr_restore(flags);
}
