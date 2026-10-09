#include "paging.h"
#include "frame.h"

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
