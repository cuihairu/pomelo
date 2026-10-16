> [Chinese](ring3.md) | **English**

# 14 · Real isolation: ring 3

Corresponding code: `boot/gdt.c`, `kernel/sched/`, `kernel/mm/paging.c`, `kernel/char.c`, `apps/probe/`

## The shell moves into its own address space

[Chapter 13](/guide/paging.en) put the paging mechanism in place: the low 16 MB identity-mapped, the kernel's exclusive half. This chapter moves the task out of that half of the world — from here on it runs in ring 3, in its own page directory, separated from the kernel by a wall the CPU itself stands guard over. Not a line of the shell's code changed: it only ever sent messages. This is the payoff of the microkernel's structural investment — it played by the rules back then, and now the upgrade is free — and in the end fs and tty collect the same dividend too (see the end of the chapter).

## A user program is a binary linked at 0x40000000

User programs are linked at `USER_BASE = 0x40000000`; that is their own world. The production pipeline has three steps:

```sh
ld -T tools/user.ld -o ushell  ...   # link into a user ELF
objcopy -O binary ushell ushell.bin  # shave it down to a flat binary
ld -r -b binary -o ushell_blob.o ushell.bin   # stuff it back into the kernel as data
```

Both designs live in `tools/user.ld`. First, **the entry point is written into the file header**: the `.hdr` segment rides in front, its contents a constant — the entry address — so the kernel reads the first four bytes of the blob and knows where to jump, without parsing ELF. Second, **.bss is folded into .data**: a flat binary carries no NOBITS segment, so uninitialized variables are merged into a segment with content — the bytes in the file are everything the task will ever touch; the kernel copies them and uses them at once, with no second step of "zero-filling afterwards".

## The other half of the GDT

`boot/gdt.c` now holds six descriptors, and the three newcomers each have their job:

```c
{0xFFFF, 0, 0, 0xFA, 0xCF, 0},   /* 0x18: code, ring 3 */
{0xFFFF, 0, 0, 0xF2, 0xCF, 0},   /* 0x20: data, ring 3 */
{0x0067, 0, 0, 0x89, 0x40, 0},   /* 0x28: TSS, filled in below */
```

The ring 3 segments, like the kernel segments, span 4G — **the wall is not built from segments, it is built from pages**; segments only lower the privilege level from 0 to 3. The TSS has one field of use: `esp0`, the kernel stack the CPU automatically switches to when ring 3 knocks (int 0x80 or an interrupt). One kernel stack per task; the TSS holds only the current task's, rewritten at every switch.

## Born into ring 3

There is no longer any such thing as a "ring 0 task" in the kernel: `task_start` (kernel/sched/sched.c) lays down, for each new task, not an ordinary stack but a complete iret frame:

```c
*--sp = USER_DS;              /* ss: the frame iret walks bottom-up */
*--sp = ustack_top;           /* user esp */
*--sp = flags;                /* eflags: EFLAGS_USER, maybe + IOPL */
*--sp = USER_CS;              /* user cs */
*--sp = entry;                /* user eip */
*--sp = (u32)ring3_entry;     /* switch_to's ret target */
```

`switch_to` pops registers as usual and `ret`s into `ring3_entry` (switch.S), which does only two things: swap ds/es for the user data segment, then `iret` — the CPU sees RPL=3 on the stack and switches eip and privilege level together, landing in user code. From then on the only way back into the kernel is the 0x80 gate.

## Switching tasks is switching worlds

The scheduler switches two more things, plus one iron rule (kernel/sched/sched.c):

```c
static void sched_switch(int next) {
    intr_disable();
    int prev = cur;
    cur = next;
    tss_esp0_set((u32)(kstacks[next] + STACK_BYTES));
    load_cr3(tasks[next].pdir ? tasks[next].pdir : (u32)kernel_pdir);
    switch_to(&tasks[prev].esp, tasks[next].esp);
}
```

The moment cr3 changes, the page tables change, and whoever 0x40000000 points at in the user region changes with it — two user tasks share one virtual address and cannot see each other; that is exactly what isolation means. The iron rule is that first line, `intr_disable`: if the clock interrupts in the middle of a switch, it stacks another `sched_switch` on top of the half-finished switch, and the interrupted layer's esp is already saved into the old task's seat — from then on nobody claims it. The way back also runs with interrupts off: a task swapped in during those lines goes on living through `switch_to`'s `cli` until `iret` restores its own EFLAGS from the frame, and only then do interrupts come back.

Where does the page directory come from? `pdir_user_new` (kernel/mm/paging.c) takes a directory frame and a user page table, copies the kernel's four PDEs verbatim, and then copies the mirror frame by frame into user frames. What it borrows is still the identity-map dividend: all frames sit in the low 16 MB, visible to the kernel, so building the table and copying never have to switch cr3 midway.

## Self-certification at boot: probe

Saying the wall is strong does not count. `apps/probe/` is a user program that comes on purpose to ram the wall: first it properly prints a line through the tty service, then it reaches out and touches the kernel's .bss:

```c
volatile u32 *kernel = (volatile u32 *)0x101000;   /* kernel .bss */
u32 s = *kernel;                                   /* #PF right here */
```

That address is a **kernel-exclusive** page in its page directory; CPL=3 touching it is a page fault on the spot. The real log of every boot:

```
pomelo booting (multiboot magic=2badb002)
paging on: low 16m identity
task 4 killed: page fault at 101000 (eip=40000055)

pomelo shell - type 'help'
```

Look at `eip=40000055`: the offending code lives in the user region, the offending address in the kernel region — the wall is built exactly in between. fs, tty, shell run as usual; smoke and nightly both list these two lines as must-checks — isolation has gone from "discipline" to "test assertion".

## The rules at the door

The only door from ring 3 into the kernel is `int 0x80`, and the door carries `DPL=3` (flags 0xEE) — users may knock, and a hardware interrupt gate (DPL=0) under their knock is a general protection fault. As for the 13/14 pair of exceptions, each now has its own dedicated path; the verdict is in `intr.c`:

```c
static void task_fault(struct regs *r, const char *what, u32 addr) {
    if ((r->cs & 3) == 0)
        panic(what, r);            /* the kernel itself is wrong: halt */
    intr_disable();
    kprintf("task %d killed: %s at %x (eip=%x)\n", cur, what, addr, r->eip);
    tasks[cur].state = ST_FREE;
    sched_next();
}
```

Kernel page fault = kernel bug = halt; user page fault = one task asking for it = kill one, everyone else is fine. `sched_next` turns around and schedules another task; the dead task's stack is never picked up again.

## The last step: the services move in too

After the shell's demonstration, the mechanism is immediately rolled out to all tasks; the four programs are now all ring 3 blobs, each living in its own address space. Two ledgers are worth writing down on their own:

**Console hardware taken over by the kernel** (`kernel/char.c`). The interrupt handler runs inside "whatever address space happens to be on the CPU at that moment" — the fs's page directory has no tty code in it, and to say it the other way around, if the ISR that handles the keyboard for tty still lived in tty, the moment it lands on a beat where fs is running would be a page fault. So the small piece that touches ports and answers IRQs (keyboard decoding plus serial) can only move into the kernel, which all address spaces share, turning characters into `TTY_CHAR` messages cast to tty; output goes the other way — the kernel opens one `sys_write` for all tasks. From here on, no service touches a port at all.

**fs's keyhole: IOPL=3**. The disk driver and the file service live together; this design has been kept since Chapter 1; but `in`/`out` in ata.c are forbidden by default in ring 3. The x86 key is IOPL in eflags: the kernel sets `IOPL=3` (`EFLAGS_USER_IOPL`) only in the birth eflags of fs alone; every other task still cannot touch a single port.

As for the last item on the list — the kernel copy in IPC — it was actually written right from day one: a message has always been the kernel copying the sender's `*m` into its own queue and then copying it to the receiver (`enqueue` in `kernel/ipc/ipc.c`). In the past the two sides shared an address space, so this copy looked redundant; now sender and receiver sit behind their own page directories, and the same lines of code are doing "delivering a message across address spaces." The structure held; when the mechanism was added, not one line of IPC changed.

The upgrade list of [Chapter 1](/guide/intro.en) is now fully paid off, every item.

## Files in this chapter

```
boot/gdt.c/.h           ring 3 segments, TSS, esp0
tools/user.ld           user linker script (.hdr entry + .bss folding)
kernel/sched/sched.c    task_start's iret frame, eflags/IOPL, cr3/esp0 switching
kernel/sched/switch.S   ring3_entry
kernel/mm/paging.c      pdir_user_new: one world per task
kernel/char.c           console hardware absorbed: interrupts become messages, output becomes a syscall
apps/probe/probe.c       the boot-time wall-ramming demonstration
```

The whole machine now lives in ring 3. But the story has one more page: what to do when a service crashes? The next chapter, [fall down and get back up](/guide/restart.en), answers exactly that — standing it back up by its name.
