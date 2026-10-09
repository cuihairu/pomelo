#ifndef POMELO_PAGING_H
#define POMELO_PAGING_H

#include "../types.h"

/* Two-level x86 paging: 1024 entries per table, 4K pages, 4M per PDE. */

#define PTE_P  1
#define PTE_RW 2
#define PTE_US 4

/* The low 16 MB are identity-mapped in every page directory: the kernel,
 * its static stacks and all page tables themselves live below this line,
 * so ring 0 can always reach them no matter whose cr3 is loaded. */
#define SPAN  (16u << 20)
#define NPT   (SPAN >> 22)               /* 4 page tables cover the span */

extern u32 kernel_pdir[1024];            /* the boot directory, ring 0 only */

void paging_init(void);                  /* build the map, flip CR0.PG */
void load_cr3(u32 pa);                   /* switch address spaces */
u32  cr2_fault(void);                    /* faulting address, for the handler */

#endif
