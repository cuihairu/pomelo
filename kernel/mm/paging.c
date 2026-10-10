#include "paging.h"
#include "frame.h"
#include "../string.h"
#include "../sched/sched.h"

/* The kernel's own page directory: identity map of the low 16 MB,
 * supervisor-only, read-write. Identity is the honest first step — every
 * address the kernel already uses keeps working, and the map is small
 * enough to see in one glance. Per-task directories (ring3) copy these
 * PDEs wholesale, so the kernel half of the world is shared by all. */

u32 kernel_pdir[1024] __attribute__((aligned(4096)));
static u32 lo_pt[NPT][1024] __attribute__((aligned(4096)));

void paging_init(void) {
    frame_init();
    for (u32 i = 0; i < NPT; i++) {
        kernel_pdir[i] = (u32)lo_pt[i] | PTE_P | PTE_RW;
        for (u32 j = 0; j < 1024; j++)
            lo_pt[i][j] = ((i << 22) | (j << 12)) | PTE_P | PTE_RW;
    }
    load_cr3((u32)kernel_pdir);
    __asm__ volatile("movl %%cr0, %%eax\n"
                     "orl  $0x80000000, %%eax\n"   /* CR0.PG */
                     "movl %%eax, %%cr0"
                     ::: "eax", "cc");
}

void load_cr3(u32 pa) {
    __asm__ volatile("movl %0, %%cr3" :: "r"(pa) : "memory");
}

u32 cr2_fault(void) {
    u32 addr;
    __asm__ volatile("movl %%cr2, %0" : "=r"(addr));
    return addr;
}

/* Virtual to physical, one page-table walk, for the caller's own space.
 * DMA descriptors (the e1000) want machine addresses, but a ring 3 task
 * only ever sees its translation -- asking the kernel is the honest way
 * to get one. Returns 0 when the mapping is not present. */
u32 v2p(u32 va) {
    u32 *de = (u32 *)tasks[cur].pdir;            /* pa == va below 16 MB */
    if (!de || !(de[va >> 22] & PTE_P)) return 0;
    u32 *te = (u32 *)(de[va >> 22] & ~0xFFFu);
    u32 e = te[(va >> 12) & 0x3FF];
    if (!(e & PTE_P)) return 0;
    return (e & ~0xFFFu) | (va & 0xFFFu);
}

/* Map one page of device memory into the caller's space. The physical
 * page number picks the slot in the MMIO window, so the same page always
 * lands at the same address and remapping is idempotent. (Real hardware
 * would also want the mapping uncacheable; this machine runs without a
 * cache, which is one cheat the emulator lets us keep.) */
u32 mmio_map(u32 pa) {
    u32 va = MMIO_BASE + ((pa >> 12) & 0x3FF) * 4096;
    u32 *de = (u32 *)tasks[cur].pdir;
    if (!(de[va >> 22] & PTE_P)) {
        u32 pt = frame_alloc();
        if (!pt) return 0;
        memset((void *)pt, 0, 4096);
        de[va >> 22] = pt | PTE_P | PTE_RW | PTE_US;
    }
    u32 *te = (u32 *)(de[va >> 22] & ~0xFFFu);
    te[(va >> 12) & 0x3FF] = (pa & ~0xFFFu) | PTE_P | PTE_RW | PTE_US;
    return va;
}

/* A page directory for one user image. The kernel PDEs are shared, so
 * ring 0 sees its whole world from any task; the user region gets fresh
 * frames holding a copy of the image, plus a stack frame. Identity saves
 * the day again: every frame below 16 MB is writable right here, so the
 * tables and the copy are built without ever switching cr3 mid-setup.
 * Returns the physical directory address, 0 on failure. */
u32 pdir_user_new(u32 blob_pa, u32 bytes) {
    u32 dir = frame_alloc();
    u32 pt = frame_alloc();
    u32 stf = frame_alloc();
    u32 frames = (bytes + 4095) / 4096;
    if (!dir || !pt || !stf || frames > 511) {   /* stack frame sits at 511 */
        if (dir) frame_free(dir);
        if (pt) frame_free(pt);
        if (stf) frame_free(stf);
        return 0;
    }

    u32 *de = (u32 *)dir;                        /* pa == va below 16 MB */
    memset(de, 0, 4096);
    for (u32 i = 0; i < NPT; i++) de[i] = kernel_pdir[i];
    memset((void *)pt, 0, 4096);
    de[USER_BASE >> 22] = pt | PTE_P | PTE_RW | PTE_US;

    u32 *te = (u32 *)pt;
    for (u32 f = 0; f < frames; f++) {
        u32 fr = frame_alloc();
        if (!fr) return 0;      /* out of memory mid-image: leak the frames,
                                   the machine is done for anyway */
        memcpy((void *)fr, (void *)(blob_pa + f * 4096), 4096);
        te[f] = fr | PTE_P | PTE_RW | PTE_US;
    }
    te[((USER_STACK_TOP - 4096) >> 12) & 0x3FF] = stf | PTE_P | PTE_RW | PTE_US;
    return dir;
}
