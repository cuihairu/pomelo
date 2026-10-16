> [Chinese](restart.md) | **English**

# 15 · Fall down and get back up

Corresponding code: `kernel/sched/prog.c`, `apps/shell/cmd_proc.c`

## After a crash, who is it that picks it back up

The microkernel's signature promise is only one sentence: a service crashes, the kernel does not die, and you restart that service on its own. Every chapter before this one has been stocking the material for it — an independent page table per service, a kernel that is only an IPC messenger, and a fault scene that kills one and leaves no corpse (a dead task's stack is never resumed). This chapter installs the last piece: **start the program again, by name**.

## The registry: a list of capabilities, not an address book

Six blobs are embedded in the kernel image. At boot, `boot/main.c` registers them into one table:

```c
prog_add("fs",    _binary_ufs_bin_start,    _binary_ufs_bin_end,
         EFLAGS_USER_IOPL);     /* the ata driver needs port rights */
prog_add("tty",   _binary_utty_bin_start,   _binary_utty_bin_end,
         EFLAGS_USER);
prog_add("shell", _binary_ushell_bin_start, _binary_ushell_bin_end,
         EFLAGS_USER);
prog_add("probe", _binary_uprobe_bin_start, _binary_uprobe_bin_end,
         EFLAGS_USER);          /* born to crash, revivable by hand */
prog_add("net",   _binary_unet_bin_start,   _binary_unet_bin_end,
         EFLAGS_USER_IOPL);     /* pci config + e1000 mmio need ports */
prog_add("netsh", _binary_unetsh_bin_start, _binary_unetsh_bin_end,
         EFLAGS_USER);          /* the net service's first customer */
```

This table is a **list of capabilities**: a task may only ask the kernel by name to start a program that is already registered — what is handed over is a word, not an address, so user mode can never get the kernel to map arbitrary memory. The kernel image itself is the trusted source; the registry nails "who can be started" down at compile time.

## A dead service returns to its own seat

`sched_spawn` (kernel/sched/prog.c) has one rule for picking a seat: **look first for the program's old seat**. Every seat in the task table records "who has lived here", and a dead task leaves a name but no corpse — the seat is empty, the name is still there:

```c
for (int i = 1; i < NTASK; i++)     /* the old seat first */
    if (tasks[i].prog == p && tasks[i].state == ST_FREE) { slot = i; break; }
```

So fs dies at seat 1 and after the restart it is still seat 1: **the TID is stable, and every message sent to TID_FS still gets through**. "Restart a service" is therefore a complete recovery story, not a restart that pretends it happened. If the seats are full or the name does not exist, `spawn` returns -1 honestly and does nothing more.

Rebirth comes with one hygiene rule too: when `task_start` lands, `msgq_reset` clears the inbox to empty — letters unread from the previous life, not a single one is left for this one. Stable TIDs are what let the **living** find the door, not what let **old letters** be delivered through the new door; the half-finished protocol left over from the dead round is cremated along with the old life.

The birth path is therefore reduced to one road: `boot/main.c` goes `prog_add` first and `sched_spawn` second as well — boot is just the first one to pull the curtain, and it is the same door as the one knocked from inside the shell.

## Two mirrors: SYS_PS and SYS_SPAWN

Two rows are added to the syscall table:

```c
SYS_SPAWN    = 7,   /* ebx=name: start a registered program */
SYS_PS       = 8,   /* ebx=buf, ecx=max: task-table snapshot */
```

`SYS_PS` is the read-only mirror: the kernel copies the task table to the caller seat by seat, one line per seat (`struct ps_entry`: tid, state, the program name that lived there). `SYS_SPAWN` is the only door. The shell wraps them into two commands:

```
pomelo> ps
tid program  state
 1 fs       blocked
 2 tty      blocked
 3 shell    ready
 4 probe    free
 5 net      blocked
 6 netsh    blocked
 7 -        free
pomelo> spawn probe
task 4 killed: page fault at 101000 (eip=40000055)
probe is task 4 now
pomelo> probe: alive at ring 3
```

Look at this screen: fs and tty are both `blocked` — they are asleep on `sys_recv`, waiting for a message to come knocking; probe is `free` — that time at boot when it rammed the wall and killed itself, the seat is empty; `spawn probe` picks it back up at seat 4, it shouts "I am alive" once more and hits the wall once more. **Fall down, get back up, fall down again** — the whole way without rebooting the machine, without touching one line of kernel state. smoke and nightly list these three lines (alive twice, kill twice) as must-checks.

## A concurrency fixed along the way

`frame_alloc` used to run naked: no interrupt-disabling protection at all. Before, nobody used it concurrently and it was peaceable; `spawn` can be launched from a shell syscall, and the clock can take the CPU away in the middle of a syscall at any moment — two tasks walking into `pdir_user_new` at once, and the same frame of memory would be awarded to two worlds. Now the allocator has one pair of `intr_disable/intr_enable` around it, and taking a seat (`task_start`) is also done with interrupts off — "the check and the grab must happen in one breath", the same source as the principle behind blocking in [Chapter 4](/guide/scheduling.en).

## Files in this chapter

```
kernel/sched/prog.c     the registry, sched_spawn, ps_snapshot
kernel/syscall/syscall.h SYS_SPAWN / SYS_PS and their wrappers
apps/shell/cmd_proc.c   the ps and spawn commands
```

There is no new chapter after this one. Run the nightly screenshot from [build and run](/guide/build-and-run.en) end to end, change one line of state while watching the `ps` output, then `spawn` one back — the microkernel's fault-tolerance story, you have just played it through yourself.
