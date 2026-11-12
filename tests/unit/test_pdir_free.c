/* unit-pdir-free: the task-death frame leak the November audit demanded.
 * A task's whole world -- page directory, user page tables, the image
 * copy, the stack -- was never returned when the task died, so a few
 * spawn/crash cycles drained the 16 MB frame bitmap until no spawn could
 * succeed. pdir_free is the teardown; this test builds a real page
 * directory and its tables at low identity-mapped addresses (mmap below
 * SPAN, which Linux permits past mmap_min_addr), walks them through the
 * production paging.c, and checks the frame_free calls that come out.
 *
 * The walk must return every RAM frame the dir owns (image, stack, the
 * page tables, the dir itself) and touch nothing else: the shared kernel
 * PDEs (indices 0..NPT-1) and device mappings (PTE pa >= SPAN, from
 * mmio_map) are not bitmap frames and must be left alone. */

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "kernel/mm/paging.h"
#include "kernel/sched/sched.h"

/* The frames pdir_free returns. paging.c calls frame_free; the test
 * supplies its own (no frame.c, no bitmap) so the calls are recorded. */
static u32 freed[64];
static int nfreed;
void frame_free(u32 pa) { if (nfreed < 64) freed[nfreed++] = pa; }
u32  frame_alloc(void) { return 0; }       /* not reached on the free path */
void frame_init(void) {}

/* paging.c also reads the task table and the current seat. */
struct task tasks[NTASK];
int cur = -1;

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

static int was_freed(u32 pa) {
    for (int i = 0; i < nfreed; i++)
        if (freed[i] == pa) return 1;
    return 0;
}

/* One writable page at a fixed low address, so the walk sees the tables
 * the way the real kernel does: pa == va, below the span. */
static void map(u32 at) {
    void *p = mmap((void *)at, 4096, PROT_READ | PROT_WRITE,
                   MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED || p != (void *)at) {
        printf("FAIL: cannot mmap %x (%p)\n", at, p);
        fails++;
    }
}

#define DIR   0x100000
#define UTBL  0x110000    /* user region page table (PDE 256) */
#define MTBL  0x120000    /* mmio window page table (PDE 258) */
#define KTBL  0x130000    /* a fake shared kernel table (PDE 0) */
#define IMG0  0x200000
#define IMG1  0x201000
#define STK   0x202000
#define IMG2  0x203000
#define DEV   0xFE000000  /* a device mapping, above the span */

int main(void) {
    map(DIR); map(UTBL); map(MTBL); map(KTBL);
    map(IMG0); map(IMG1); map(STK); map(IMG2);

    memset((void *)DIR,  0, 4096);
    memset((void *)UTBL, 0, 4096);
    memset((void *)MTBL, 0, 4096);
    memset((void *)KTBL, 0, 4096);

    u32 *de = (u32 *)DIR;
    de[0]   = KTBL | PTE_P | PTE_RW;                 /* shared: must survive */
    de[256] = UTBL | PTE_P | PTE_RW | PTE_US;         /* the user image */
    de[258] = MTBL | PTE_P | PTE_RW | PTE_US;         /* the mmio window */

    u32 *ute = (u32 *)UTBL;
    ute[0]   = IMG0 | PTE_P | PTE_RW | PTE_US;        /* image frame */
    ute[1]   = IMG1 | PTE_P | PTE_RW | PTE_US;        /* image frame */
    ute[511] = STK  | PTE_P | PTE_RW | PTE_US;        /* stack frame */

    u32 *mte = (u32 *)MTBL;
    mte[0] = DEV | PTE_P | PTE_RW | PTE_US;           /* a card, not a frame */
    mte[1] = IMG2 | PTE_P | PTE_RW | PTE_US;          /* a real RAM frame */

    nfreed = 0;
    pdir_free(DIR);

    CHECK(nfreed == 7, "pdir_free returns exactly the owned frames");
    CHECK(was_freed(DIR),   "the page directory is returned");
    CHECK(was_freed(UTBL),  "the user page table is returned");
    CHECK(was_freed(MTBL),  "the mmio page table is returned");
    CHECK(was_freed(IMG0),  "image frame 0 is returned");
    CHECK(was_freed(IMG1),  "image frame 1 is returned");
    CHECK(was_freed(STK),   "the stack frame is returned");
    CHECK(was_freed(IMG2),  "a RAM frame in the mmio table is returned");
    CHECK(!was_freed(KTBL), "the shared kernel table is not returned");
    CHECK(!was_freed(DEV),  "the device page is not returned");

    if (fails) { printf("unit-pdir-free: %d failure(s)\n", fails); return 1; }
    printf("unit-pdir-free: all ok\n");
    return 0;
}
