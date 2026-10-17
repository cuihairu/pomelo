# 04 · Scheduling

Corresponding code: `kernel/sched/`

## A task is nothing but a stack

Words like process and thread are easy to bluff with. Peel them open and a runnable task is just three things: **a piece of code, a stack, and a saved register scene**. Pomelo's task table is small:

```c
struct task {
    u32        esp;       /* where the scene is saved: esp is stored here at swap time */
    u32        pdir;      /* page directory, 0 = use the kernel's own; Chapter 14 */
    int        state;     /* FREE / READY / BLOCKED */
    int        woke;      /* touched by sched_wake; irq_wait relies on it to tell "woken for a message" */
    const struct prog *prog;  /* which program this seat belongs to; Chapter 15 */
    struct msgq inbox;    /* its own message queue, detailed in the IPC chapter */
};
```

Stacks are carved out of one big static array (4 KB per task) — this simple kernel does not bother with an allocator; even stacks have their size measured out in advance.

The whole secret is `esp`. The so-called "switch" is just **swap esp and `ret`** — and the CPU carries on with the scene of another stack. `pdir` is the other half: when switching, besides swapping the stack you also swap cr3, and address spaces travel between tasks (detailed in Chapter 14).

## Context switch: a piece of assembly you cannot skip

`kernel/sched/switch.S` is the heart of the whole scheduler, under twenty lines:

```asm
switch_to:
    cli                    # a clock tick in the middle of the stack swap ruins both stacks
    movl  4(%esp), %eax    # &old->esp
    movl  8(%esp), %edx    # new->esp
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    movl  %esp, (%eax)     # store the current esp into old->esp
    movl  %edx, %esp       # switch to the new task's stack
    popl  %ebx
    popl  %esi
    popl  %edi
    popl  %ebp
    ret                    # continue with the new task's return address
```

One call pushes the **callee-saved registers**, swaps the stack, and pops them back — when the `ret` goes out of the new stack, the CPU is already running another task. The callee-saved registers guaranteed by the C compiler happen to be exactly these four, so pushing them is enough. The `cli` disables interrupts to protect the few instructions of the stack swap; the first `ret` of a brand-new task lands on `ring3_entry`: it sets up the user data segments, then `iret` drops straight into ring 3 — in this world there are no ring 0 tasks to enter (Chapter 14).

## When to switch

Pomelo is a hybrid of **cooperative + preemptive**, but the main act is preemption:

- **Clock preemption**: every IRQ0, the scheduler decides whether to switch;
- **Voluntary yield**: `yield()` (the syscall `SYS_YIELD`) lets a task busy-waiting hand over to someone else.

The real decision is in `sched_tick`:

```c
void sched_tick(void) {
    if (cur < 0) return;
    int next = pick_ready();          /* the first READY task after cur */
    if (next >= 0) sched_switch(next);
}
```

Plain round-robin: starting from the current task, look forward for the first one not blocked. 10 ms per round — nobody gets to hog the CPU.

## Blocking: the legitimate reason to hand over the CPU

When a task is waiting on an IPC message or an interrupt, it sets its state to `BLOCKED` and calls `sched_next` to hand over. The scheduler skips it:

```c
void sched_wake(int tid) {
    if (tasks[tid].state == ST_BLOCKED) {
        tasks[tid].state = ST_READY;
        tasks[tid].woke = 1;          /* tell irq_wait: I'm the one who called you */
    }
}
```

What `irq_raise` and `ipc_send` do is exactly the `sched_wake` thing — **both interrupts and messages can wake a task**. It is the same move this book keeps repeating.

## No one to run: go to sleep, do not spin

What if every task is BLOCKED? That is not a deadlock — every service is waiting on its own hardware. `sched_next` stops the CPU into `hlt`: with interrupts on, the CPU sleeps until the next interrupt (at worst the 100 Hz clock) calls someone again. There is one more delicate way out: if the clock happens to wake exactly the task that is in the middle of "blocking itself", `sched_next` finds the field already READY and returns on the spot — the blocking is cancelled on the spot. Judging, sleeping, re-checking must be done in one breath with interrupts disabled.

## The heartbeat is also a clock

Every time a clock interrupt arrives, `sched_clock_tick` adds one to `ticks` — **the kernel's heartbeat count is the uptime**. The `uptime` command hands it out:

```c
void sched_clock_tick(void) {
    ticks++;
    sched_tick();
}

u32 sched_uptime(void) {
    return ticks / HZ;
}
```

Note one easy pitfall: `SYS_YIELD` also calls `sched_tick` (yielding is "pretending your time slice is used up") but **does not count the clock**. fs's polling waits yield hundreds or thousands of times per second; if yielding also counted, this clock would run several times faster than the wall clock — courtesy is not the passage of time. Only IRQ0 counts the clock.

This path is also worth memorizing next to `ls`: **time is the kernel's state, so it goes straight through a syscall; files are the fs's state, so you must send messages**. The path follows the state — the same criterion from beginning to end of this book.

## Files in this chapter

```
kernel/sched/sched.h    the task struct, the task table and the API
kernel/sched/sched.c    taking a seat to build a task, round-robin, block/wake
kernel/sched/switch.S   switch_to register stack swap
kernel/sched/prog.c     the program registry, sched_spawn, ps_snapshot (Chapter 15)
```

The next chapter: [IPC](/guide/ipc).
