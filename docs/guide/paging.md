# 13 · Paging comes online

Corresponding code: `kernel/mm/`

## The first thing only the kernel can do

[Chapter 1](/guide/intro) said it: the criterion for dividing kernel from services is "what every task cannot get around". Memory translation is exactly such a thing: one machine wants to use the address 0x40000000 for several different jobs at the same time, and only the MMU inside the CPU can adjudicate. So `kernel/` gains a fifth mechanism — paging. The previous four mechanisms got one chapter each; this one gets two: this chapter owns "how addresses get translated", the next owns "how the world gets swapped".

## Frames: a bitmap

Before paging there have to be pages to hand out. Physical memory is cut into 4 KB frames and the allocator is a bitmap — the whole outfit of `frame.c`:

```c
static u8 used[NFRAMES / 8];
static u32 first_free;

extern u8 kernel_end[];                  /* boot/kernel.ld */

void frame_init(void) {
    first_free = (((u32)kernel_end + 4095) & ~4095u) / 4096;
    for (u32 f = 0; f < first_free; f++) mark(f);
}
```

`kernel_end` is a symbol from the linker script: wherever the kernel image ends, the frames below that point (BIOS data, VGA text, the kernel's own .bss) are all marked used. The bitmap is scanned from head to tail and the first empty slot is the allocation result — at the scale of 16 MB of memory and a few dozen user frames, that is less fuss than a linked list and more honest than a textbook. Frames that were lent out also have a way back: `frame_free` clears the bit and, in passing, pulls the `first_free` watermark up to it — so a returned frame does not land beyond the scan's starting point and go unclaimed from then on. At the moment there is only one guest: when a user directory fails partway through creation, the code pages, page table and stack frames are handed back the way they came.

This bitmap needs no imagining across the pages of a book — the `mem` command in the shell is a mirror of it:

```
pomelo> mem
frames: 3791 of 4096 free
```

`SYS_MEM` counts the bitmap over, counting with interrupts off, so the clock cannot hand out a frame halfway through and leave the three numbers failing to add up. You can see the kernel's memory with your own hands: type `mem` once, then `spawn probe`, then `mem` again — the frames that went missing are probe's new address space (code pages + page table). The allocator goes from "a piece of code in a book" to "a visible account on the machine".

## The identity mapping: not one address changes

The kernel's own page directory does the most conservative thing: map the low 16 MB **identically**, to the same addresses.

```c
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
                     "movl %%eax, %%cr0" ::: "eax", "cc");
}
```

The moment CR0's PG bit is flipped, the MMU starts working and the kernel notices nothing — every instruction fetch already written, every `outb`, every stack operation: the address is the same before and after translation. That is deliberate: `PTE_RW` but without `PTE_US`, so the low 16 MB belongs to the kernel alone; the moment ring 3 walks in, this half of the world closes its door to it. Let the mechanism stand first with addresses unchanged; only then does the next chapter get to move addresses.

This directory has one more identity: the **kernel half** of every user page directory. The next chapter will show that creating a user directory is just copying these four PDEs over verbatim — the kernel looks the same in the address space of any task.

## Device windows and physical addresses: mmio_map and v2p

These two pieces of furniture were added when the network chapter moved in, but they are pure paging machinery, so their home is here.

PCI device registers are mapped high in the physical address space (the e1000's MMIO BAR is reported by the configuration space, up around the top of 4 GB), while every page directory only dutifully maps the low 16 MB — high physical addresses are out of the kernel's own reach as well. Whoever wants to touch the device opens the window themselves:

```c
u32 mmio_map(u32 pa);   /* map one page of device memory into the caller's space, return the virtual address */
u32 v2p(u32 va);        /* walk the current task's page table, translate the virtual address back to a physical address */
```

`mmio_map` opens a window one PDE wide at `MMIO_BASE` (0x40800000): the slot takes the low 10 bits of the physical page number, so the same physical page always lands in the same virtual address and repeated mapping is idempotent; the page table entry is `P|RW|US` — apart from the task's own code and data, this is the only mapping ring 3 can reach that belongs to someone else. A 4 MB window, 4 KB per slot, 1024 slots in total, and the price is plainly marked: as soon as the physical page number rises one step past 4 MB, two pages squeeze into the same slot. Just right for an e1000 with only one MMIO BAR; that reckoning is written in the comment.

The gate itself checks no identity: `SYS_MMIO` lets any task through to any physical page. The licensing regime of version 0.1 is not yet law — a restriction only counts once it has been spoken.

`v2p` answers the other half of the question: DMA descriptors must carry a **machine address**, while the driver holds only its own virtual addresses. It walks the current task's directory and page table (the old pa==va trick lets the kernel read the tables without switching cr3); if the page is not there it returns 0. Translation is the kernel's turf; the driver asks, and the kernel tells it straight.

(Real hardware would also have to give the device window its uncacheable bit; this machine has no cache, a bill the emulator saves us, and the comment in `paging.c` admits it too.)

## What to do when a page fault arrives

Once paging is on, the CPU has one more class of thing to report: exception 14, the page fault. The dispatch table in `intr.c` now looks like this:

```c
if (r->int_no == 14) {                /* page fault */
    task_fault(r, "page fault", cr2_fault());
    return;
}
```

`cr2_fault()` reads CR2 — the CPU has recorded the faulting address there, and that is the only physical evidence the MMU delivers. What happens next depends on who is at fault: a page fault by the kernel itself is a kernel bug, panic and stop the machine; a page fault by a ring 3 task, the task is the one that gets killed. The verdict logic is in the next chapter.

## Files of this chapter

```
kernel/mm/frame.c/.h  the physical frame bitmap allocator
kernel/mm/paging.c/.h the kernel page directory, the identity mapping, cr3 and CR0
```

The next chapter: [Real isolation: ring 3](/guide/ring3).
