# 01 · What kind of kernel is this

## Why the filesystem does not live in the kernel

Operating systems textbooks draw the filesystem inside the kernel. pomelo refuses: the filesystem and the terminal are user-mode programs, living in `servers/`, at the same level as your shell. The rule for the division is just one: **only what every task cannot get around goes into the kernel**.

The filesystem is only useful to programs that read files, and the terminal is only useful to programs that want to type. Everything runs fine without them. But "hand a message to the neighboring task" and "whose turn is it on the CPU" — everyone goes through those, so IPC and scheduling stay in the kernel. Keep the error-prone code out of the way and when it breaks it cannot hurt the core: break the fs however you like, the kernel is still there.

The pomelo kernel does only five things:

| Stays in the kernel (kernel/) | Moved to user mode (servers/, apps/) |
| --- | --- |
| Interrupt dispatch | Filesystem (servers/fs) |
| Scheduling | Terminal (servers/tty) |
| IPC (message queues) | shell (apps/shell) |
| Syscall gate | Network stack and sockets (servers/net, apps/netsh) |
| Paging (MMU) | |
| Console and mouse hardware (kernel/char.c, kernel/mouse.c) | |

Five mechanisms, plus the input hardware absorbed into the kernel — keyboard, serial and mouse interrupts can only be caught by the kernel (the reason is in [Chapter 14](/guide/ring3)). That is the only exception in this table. The whole outfit is 33 files and 2034 lines (`kernel/` 28 files, 1835 lines, plus the 5 files and 199 lines in `boot/`). Read that pile of `kernel/` and you have read all there is to the kernel; if you want to check, the end of this chapter tells you how to get the machine running.

## Where the monolithic kernel is fast, and where the microkernel is steady

Put the two designs side by side:

| | Monolithic kernel (Linux, FreeBSD) | Microkernel (MINIX 3, QNX, pomelo) |
| --- | --- | --- |
| Where the filesystem and drivers live | Kernel mode, sharing one address space | User-mode services, each its own task |
| The path of one read | A function call, straight to the disk driver | Messages: app → IPC → fs service → back |
| A driver hits a null pointer | The whole kernel goes down with it, machine reboots | The service crashes; the kernel and the other services are fine |
| How a service gets upgraded | Rebuild the kernel, reboot the machine | Just restart that one service |

The last row is not a slogan: in [Chapter 15](/guide/restart) you will pull a dead service back up yourself.

Each side keeps its own ledger. The monolithic kernel is fast: between services it is just function calls, and `read()` runs from VFS to ext4 down into the driver with no message copying and no context switch along the way. Thirty years of drivers and filesystems are there, and when something goes wrong one panic stack runs from the application through to the hardware, and gdb follows it all the way down. These are real advantages — Linux ruling the servers is no accident.

The price is the trust surface. Kernel-mode code can do anything, so its bugs can break anything: a wild pointer in a GPU driver sits equal with the scheduler, and the same line of code can flip the whole machine. Tens of millions of lines in the kernel, every one of them sharing full privilege, and after a patch you usually have to reboot, because what has to be replaced is the kernel itself.

The microkernel flips the ledger. Small and steady kernel: a two-thousand-line kernel has only so much room to be wrong, and seL4 can be formally verified — the premise is also that the kernel is small enough to verify. Fault isolation: the fs crashes, the kernel does not die, restart the fs on its own and the other tasks notice nothing; automatic restart of crashed drivers in MINIX 3 is a feature from decades ago. Modules are replaceable: Chapter 5 will show you that between shell and fs there are only messages — swap in a different fs implementation and shell does not change a line. The attack surface is small too: to get into the kernel an attacker first has to cross four mechanisms, and disk-format parsing — the classic disaster zone for vulnerabilities — is not in the kernel at all.

The price is just as plainly visible. One cross-service call has to pass through two syscalls, copy a message once and switch context twice — an order of magnitude slower than a function call. The logic of one "read a file" is scattered across several tasks, so a breakpoint cannot follow it; you have to read along the messages. That is the old complaint against microkernels, and this repository cannot dodge it.

pomelo picks the microkernel, and it is reckoning the ledger of reading code: directories are boundaries, and kernel and services are obvious at a glance. Each of the five mechanisms is under one hundred and fifty lines, so each can be written by hand and read through on its own. The message path is clear enough to draw on paper. The engineering trade-off (performance for isolation) belongs to real systems; Chapter 14 shows us turning that paper wall into a brick wall.

## How many gates a file read has to pass

What does "an application gets a service" look like end to end in a microkernel? Walk through `cat hello.txt` — this is a path that really exists in the repository:

```
apps/shell   cmd_cat:fs_open("hello.txt")
   │
   │ sys_send(FS_OPEN)         ← the syscall gate 0x80, the only entry
   ▼
kernel/ipc   puts the message in the fs task's inbox, wakes the sleeping fs
   ▼
servers/fs   do_open: looks up the in-memory inode table, hit, replies with {ino, size}
   │
   │ sys_send(shell, reply)     ← the way back is still one message
   ▼
apps/shell   sends FS_READ again, fs reads sectors through the ATA driver, data comes back in chunks
```

Worth fixing on two points. First, there is only one way for the shell to get into the fs: the syscall gate 0x80 plus a message, no other door. Second, no function call crosses a boundary anywhere along the way — the shell knows nothing about the disk, and the fs does not know who is reading. Applications get services through IPC messages; that is the entire reason the servers/ directory exists.

## Isolation delivered: everyone lives in ring 3

The services in the earlier chapters run in one shared address space, divided by **discipline** rather than hardware. From [Chapter 13](/guide/paging) the MMU comes in; by [Chapter 14](/guide/ring3) fs, tty, shell, probe, net and netsh — all six programs — have moved into ring 3 with their own page directories. The moment the shell touches kernel memory, page protection kills it on the spot, and every boot the probe program demonstrates it in front of you. fs and net can reach the ports, and only because the kernel opened one single keyhole — IOPL=3 — in the eflags it gives them at birth.

Messages are born cross-address-space too: the kernel copies the sender's message into its own queue and then copies it to the receiver, and the two sides never share a page of memory from start to finish. Structure comes before mechanism: having read the message path smoothly in the earlier chapters, this layer of mechanism is surprisingly thin.

## Who a keystroke passes through

Every chapter after this one fills in the chain below. Here is the whole picture first:

```
Keyboard interrupt ──▶ kernel/char.c   (the kernel catches the scan code and turns it into a message)
              │  SEND(TTY_CHAR)
              ▼
          servers/tty        (character → line buffer → answer the shell)
              ▲  SEND(line)      │
              │                  │ SEND(line)
   apps/shell │                  ▼
   (read a line, look up the command table, send more messages) ──▶ servers/fs  (look up the inode, read and write disk blocks)
```

The shell never touched the keyboard and never touched the disk: it only knows `SEND` and `RECV`.

## How to read this tutorial

- Each chapter corresponds to one directory; the end of the chapter lists the files in that directory.
- Every code block is taken directly out of a real file in the repository, not pseudocode.
- Read it through once in order first, then come back and fork it to change yourself.

The next chapter starts from [power-on to the first line of C](/guide/boot).
