# Architecture layers

This map separates the source by responsibility rather than by directory name. It complements the chapter sequence in [intro](/guide/intro); the [glossary](/guide/glossary) uses the same file references.

## Layer map

| # | Layer | Responsibility | Representative modules |
| --- | --- | --- | --- |
| 1 | Boot and platform | Enter C, install privilege and interrupt tables, program timers and interrupt controllers, and start the registered user programs. | `boot/boot.S`, `boot/gdt.c`, `boot/main.c` |
| 2 | Kernel mechanism | Own only the five universal mechanisms: interrupt dispatch, paging, scheduling, IPC, and the syscall gate. | `kernel/intr`, `kernel/mm`, `kernel/sched`, `kernel/ipc`, `kernel/syscall` |
| 3 | Kernel-owned input/output | Decode IRQ1/IRQ4/IRQ12 hardware events, render console output, and publish hardware events as fixed messages. | `kernel/char.c`, `kernel/mouse.c`, `kernel/vga.c` |
| 4 | User-space services | Own device drivers and state behind named request/reply message protocols in independent ring 3 address spaces. | `servers/fs`, `servers/tty`, `servers/net` |
| 5 | Applications | Turn lines, commands, and replies into calls on a service protocol or syscall. | `apps/shell`, `apps/netsh`, `apps/probe` |
| 6 | Build and packaging | Link kernel and ring 3 programs, embed user images as blobs, and create the disk image. | `CMakeLists.txt`, `tools/user.ld`, `tools/mkfs` |
| 7 | Verification and configuration | Boot QEMU, drive serial and PS/2 inputs, and check visible protocol behavior; supply runtime machine, network, and port settings. | `tools/smoke*.sh`, `tools/walkthrough.sh`, `CMakeLists.txt`, `.github/workflows/*.yml` |

## Boot and platform

`boot/boot.S` carries the multiboot header and the first 32-bit entry; `boot/gdt.c` replaces inherited descriptors with flat ring 0 and ring 3 segments plus a TSS; `boot/main.c` initializes the platform in dependency order, embeds six user images, registers them with capability flags, spawns them by name in fixed task order, and hands the CPU to `TID_FS`. Its configuration is the kernel ELF link layout in `boot/kernel.ld` plus the compile-time registry in `boot/main.c`.

**Files:** `boot/boot.S`, `boot/gdt.c`, `boot/gdt.h`, `boot/main.c`, `boot/kernel.ld`.

## Kernel mechanism

Interrupt dispatch owns the IDT, PIC remapping, PIT, exceptions, and the dispatch path that either enters the syscall table or sends an IRQ to its owner; paging owns the 16 MiB kernel identity map, frame bitmap, user directories, MMIO, and VA-to-PA translation; scheduling owns task seats, ring 3 startup frames, the program registry, preemption, blocking, and restart by name; IPC owns every task's fixed-size inbox; the syscall gate validates user pointers and exposes the 15 system operations. State is in kernel arrays: task/inbox records, the frame bitmap, program registry, IDT, IRQ ownership table, and page tables. The shared ABI lives in `kernel/syscall/syscall.h`; there is no runtime configuration file.

**Files:** `kernel/intr/intr.h`, `kernel/intr/idt.c`, `kernel/intr/irq.c`, `kernel/intr/intr.c`, `kernel/intr/stubs.S`; `kernel/mm/paging.h`, `kernel/mm/paging.c`, `kernel/mm/frame.h`, `kernel/mm/frame.c`; `kernel/sched/sched.h`, `kernel/sched/sched.c`, `kernel/sched/prog.c`, `kernel/sched/switch.S`; `kernel/ipc/ipc.h`, `kernel/ipc/ipc.c`; `kernel/syscall/syscall.h`, `kernel/syscall/syscall.c`.

## Kernel-owned input/output

Input must run in the identity-mapped kernel world, so IRQ1/IRQ4 and IRQ12 live here; they decode keyboard, serial, and mouse samples and send fixed messages to the tty service. Output reaches `SYS_WRITE` and `SYS_CLEAR`, whose kernel side writes VGA and COM1. The exception is deliberate: boot-owned input hardware is the only hardware kept in the kernel. State is the console cursor, UART configuration, a one-slot held keystroke, mouse packet state and cursor save cell. Protocol names live in the tty header; user-facing output copy is emitted by kernel code or the calling application.

**Files:** `kernel/char.h`, `kernel/char.c`, `kernel/mouse.h`, `kernel/mouse.c`, `kernel/vga.h`, `kernel/vga.c`, `servers/tty/tty.h`.

## User-space services

The fs service owns ATA PIO and the three-region disk format, loads the superblock and inode table at start, answers shell file requests, and degrades to an explicit refusal on a missing disk or bad magic. The tty service owns the line discipline and unclaimed completed lines. The net service owns PCI/e1000/MMIO/DMA, ARP, IP, ICMP, UDP, a minimal TCP, and a four-entry socket table. All three run in ring 3 and communicate only through `SYS_SEND`/`SYS_RECV`. Service protocol schemas are headers (`servers/fs/fs.h`, `servers/tty/tty.h`, `servers/net/net.h`); the disk schema is `servers/fs/format.h`. Runtime network parameters are compile-time SLIRP conventions rather than a configuration file.

**Files:** `servers/fs/format.h`, `servers/fs/fs.h`, `servers/fs/fs.c`, `servers/fs/fsrw.c`, `servers/fs/ata.c`; `servers/tty/tty.h`, `servers/tty/tty.c`; `servers/net/net.h`, `servers/net/net.c`.

## Applications

The shell is a REPL and client layer: it has a command table and thin wrappers for the tty and fs protocols. `netsh` is an event loop that opens and drives socket contexts for echo and a greeting. `probe` sends one tty message, then deliberately touches kernel memory to demonstrate isolation. Configuration is compile-time client tables and network constants (`apps/shell/shell.c`, `apps/shell/shell.h`, `apps/netsh/netsh.c`); protocol headers are shared by source inclusion rather than having an independent client protocol file.

**Files:** `apps/shell/shell.h`, `apps/shell/shell.c`, `apps/shell/fsio.c`, `apps/shell/cmd_*.c`, `apps/netsh/netsh.c`, `apps/probe/probe.c`, `apps/con.h`.

## Build and packaging

The top-level build links six freestanding flat ring 3 images, converts each into a binary, wraps it as a relocatable object, and embeds those blobs into the kernel; `tools/user.ld` puts the entry point first and folds `.bss` into `.data`. The kernel is linked by `boot/kernel.ld`; `mkfs` creates the disk image from `tools/mkfs/seed`. Build inputs are `CMakeLists.txt`, linker scripts, and seed files; there is no runtime configuration schema.

**Files:** `CMakeLists.txt`, `tools/user.ld`, `boot/kernel.ld`, `tools/mkfs/mkfs.c`, `tools/mkfs/seed/*`.

## Verification and configuration

Smoke and walkthrough scripts boot QEMU and drive serial, PS/2, mouse, network, clear, and reboot paths, then assert logs or VGA snapshots. CI reruns the same tests, adds unit tests for the host-linkable service paths, and publishes documentation. Runtime configuration is invocation-shaped: QEMU is required, `run` selects the SLIRP network and e1000 device, and smoke scripts forward host TCP/UDP ports; there is no config parser or schema file.

**Files:** `tools/smoke.sh`, `tools/smoke-ps2.sh`, `tools/smoke-mouse.sh`, `tools/smoke-net.sh`, `tools/smoke-clear.sh`, `tools/smoke-reboot.sh`, `tools/walkthrough.sh`, `tools/nightly-run.sh`, `tests/unit/test_fs.c`, `tests/unit/test_net.c`, `tests/unit/test_mkfs.sh`, `CMakeLists.txt`, `.github/workflows/*.yml`.

## Split rationale

1. **Boot from mechanisms and services.** `boot/main.c` must call GDT, IDT, PIC, PIT, paging, and spawn initialization before enabling interrupts; those are time-ordering responsibilities, not reusable kernel policy (`boot/main.c:44-62`).
2. **Kernel means universal, not merely privileged.** The source states that only what every task cannot avoid belongs in the kernel: interrupt dispatch, scheduling, IPC, the syscall gate, and paging; a file service is absent from every task's minimum path (`docs/guide/intro.md:9-17`, mirrored in `kernel/intr`, `kernel/sched`, `kernel/ipc`, `kernel/syscall`, and `kernel/mm`).
3. **Interrupt control is split from interrupt ownership.** `intr.c` programs the PIC/PIT and routes vectors, while `irq.c` installs handlers, tracks claimants and waiters, and lets a ring 3 driver delay EOI until it has quieted its device (`kernel/intr/intr.c:70-94`, `kernel/intr/irq.c:4-10`).
4. **Capabilities are separate from scheduling.** `sched.c` starts a prepared seat, while `prog.c` owns the compile-time name-to-blob registry and reuses a program's old seat, so startup and recovery have the same door without coupling CPU handoff to program lookup (`kernel/sched/sched.c:31-55`, `kernel/sched/prog.c:29-48`).
5. **IPC is isolated as a mechanism, not embedded in the scheduler.** The scheduler knows whether a seat is ready, while IPC owns the fixed message shape, inbox capacity, copy, blocking, and wake; keeping those apart lets every service protocol use one transport (`kernel/ipc/ipc.c:13-63`, `kernel/sched/sched.h:38-48`).
6. **Console input is kernel-owned, console state is service-owned.** Keyboard and serial ISRs must exist in the identity-mapped kernel world, but backspace, echo, and completed-line buffering are terminal policy and therefore live in tty (`kernel/char.c:7-14`, `servers/tty/tty.c:10-15`).
7. **File protocol and disk format are separated from block I/O.** The message API in `fs.h`, the on-disk superblock/inode schema in `format.h`, the service dispatch in `fs.c`, and the polled PIO device driver in `ata.c` have different change rates and trust boundaries (`servers/fs/fs.h:8-27`, `servers/fs/format.h:6-48`, `servers/fs/ata.c:5-14`).
8. **Network transport and socket service have different state models.** Ethernet/IP/protocol frames are stream-driven and learned from traffic, while socket contexts carry pending app requests, sequence numbers, receive buffers, and retransmission state; mixing those concerns would make the service API depend on wire framing (`servers/net/net.c:369-394`, `servers/net/net.c:437-470`).
9. **Applications remain thin clients.** The shell explicitly speaks only tty and fs protocols, and netsh's whole runtime is replies from the net service; this keeps command/UI policy out of service state machines (`apps/shell/shell.h:9-10`, `apps/netsh/netsh.c:163-174`).
10. **Build and runtime are separate layers.** The blob pipeline and disk image exist only until boot and smoke time, while kernel/services own behavior afterward; the CMake blob pipeline and mkfs are host tools, not kernel mechanisms (`CMakeLists.txt:11-48`, `tools/mkfs/mkfs.c:1-4`).
11. **Verification drives the external contract.** Smoke tests invoke the actual QEMU command surface and inspect protocol-visible outcomes, so they test configuration and behavior rather than internal functions (`CMakeLists.txt:113-153`, `tools/smoke-net.sh:18-25`).
