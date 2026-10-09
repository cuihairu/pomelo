#include "frame.h"
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
    intr_disable();   /* a tick here could hand one frame to two spawners */
    for (u32 f = first_free; f < NFRAMES; f++) {
        if (!is_used(f)) {
            mark(f);
            if (f == first_free) first_free++;
            intr_enable();
            return f * 4096;
        }
    }
    intr_enable();
    return 0;
}

void frame_free(u32 pa) {
    intr_disable();
    unmark(pa / 4096);
    if (pa / 4096 < first_free) first_free = pa / 4096;
    intr_enable();
}
