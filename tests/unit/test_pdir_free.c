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

/* The other half: pdir_user_new asks frame_alloc for its frames, so the
 * test scripts the answers out of the pool and can force a shortage by
 * keeping the pool smaller than the image wants. */
static u32 pool[64];
static int npool;
static int ncalls;
u32 frame_alloc(void) {
    if (ncalls >= npool) return 0;         /* the shortage the test wants */
    return pool[ncalls++];
}
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
#define POOL0 0x400000   /* frame_alloc's answers */
#define BLOB  0x300000   /* the image pdir_user_new copies */

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

    /* A spawn that dies of a frame shortage mid-image must come out even:
     * every frame the copy already took has to come back, dir and tables
     * with them, and the stack frame the mapping has not reached yet too.
     * frame_alloc is scripted so the shortage lands where the test wants
     * it to -- the second image frame, after dir, table, stack and the
     * first image frame are already held. */
    nfreed = 0;
    ncalls = 0;
    npool = 0;
    for (int i = 0; i < 16; i++) {
        map(POOL0 + (u32)i * 4096);
        pool[npool++] = POOL0 + (u32)i * 4096;
    }
    map(BLOB);
    map(BLOB + 4096);
    memset((void *)BLOB, 0x5A, 8192);

    nfreed = 0; ncalls = 0; npool = 4;
    CHECK(pdir_user_new(BLOB, 8192) == 0, "a mid-image shortage gives up");
    CHECK(ncalls == 4, "it got four frames and then the shortage hit");
    CHECK(nfreed == 4, "the abort returns the four frames it held");
    CHECK(was_freed(pool[0]), "the directory itself comes back");
    CHECK(was_freed(pool[1]), "the page table comes back");
    CHECK(was_freed(pool[2]), "the stack frame comes back too");
    CHECK(was_freed(pool[3]), "the image frame already copied comes back");

    /* and a directory that does build gives the stack clean bytes, so a
     * dead task cannot hand its replacement what it left on the frame */
    nfreed = 0; ncalls = 0; npool = 16;
    for (int i = 0; i < 16; i++)
        memset((void *)pool[i], 0xEE, 4096);     /* old contents */
    u32 dir = pdir_user_new(BLOB, 8192);
    CHECK(dir != 0, "enough frames and a full image build the directory");
    int dirty = 0;
    for (int i = 0; i < 4096; i++)
        if (((u8 *)pool[2])[i] != 0) dirty++;    /* pool[2] is the stack frame */
    CHECK(dirty == 0, "the user stack frame is zeroed before it is mapped");
    nfreed = 0;
    pdir_free(dir);
    CHECK(nfreed == 5, "and the directory still owns its five frames");

    if (fails) { printf("unit-pdir-free: %d failure(s)\n", fails); return 1; }
    printf("unit-pdir-free: all ok\n");
    return 0;
}
