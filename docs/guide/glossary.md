# Glossary

This table contains the architecture-level vocabulary that recurs across the source. It is deliberately concise and always points back to implementation evidence. Chinese version: [术语表](/zh/guide/glossary).

| Term | English | Definition | Evidence |
| --- | --- | --- | --- |
| 多重引导 | Multiboot | The boot-time magic/header contract that lets QEMU's direct kernel loader enter the 32-bit entry point. | `boot/boot.S:1-30` |
| 全局描述符表 | Global Descriptor Table (GDT) | A six-entry flat descriptor table that establishes ring 0 and ring 3 segments plus the TSS used for transitions to ring 0. | `boot/gdt.c:18-25` |
| 任务状态段 | Task State Segment (TSS) | The CPU-owned transition record whose `esp0` is set to the current task's kernel stack top before every user task switch. | `boot/gdt.c:27-40` |
| 程序注册表 | Program registry | The compile-time, name-keyed list of embedded user images and their birth `EFLAGS`; it is the only spawn capability. | `kernel/sched/sched.h:20-30`, `kernel/sched/prog.c:13-23` |
| 任务座位 | Task seat | One slot in the fixed task table holding saved kernel ESP, page directory, state, wake flag, program identity, and inbox. | `kernel/sched/sched.h:38-48` |
| 稳定任务标识 | Stable task ID | The invariant that restart by name prefers the dead program's prior seat so existing `TID_*` destinations continue to work. | `kernel/sched/prog.c:25-48` |
| 轮转抢占 | Round-robin preemption | The 100 Hz PIT heartbeat selects the next ready seat; a yield reuses handover without advancing the kernel clock. | `kernel/sched/sched.c:57-103` |
| 内核消息 | Kernel message | A 72-byte value copied into a fixed 16-slot ring inbox; the kernel fills `src` and sends as signed source 0 from interrupt paths. | `kernel/ipc/ipc.h:6-22`, `kernel/ipc/ipc.c:29-37` |
| 收件箱 | Inbox | Per-task finite message ring that can enqueue, block the receiver, and wake it atomically. | `kernel/ipc/ipc.h:19-22`, `kernel/ipc/ipc.c:13-27` |
| 等待发送 | Send-wait | A client-side retry loop that yields while the destination inbox is full, preventing a silently lost request or stranded reply. | `kernel/syscall/syscall.h:62-68` |
| 系统调用门 | Syscall gate | The sole DPL 3 interrupt gate (`0x80`) through which ring 3 can enter kernel service dispatch. | `kernel/intr/idt.c:61-63`, `kernel/syscall/syscall.h:39-44` |
| 用户指针校验 | User pointer validation | Syscall boundary check that requires an address range to lie in the user region and every page to be mapped in the caller's page directory. | `kernel/syscall/syscall.c:21-35` |
| 中断所有权 | IRQ ownership | The distinction between kernel-installed handlers acknowledged immediately and ring 3 claimants that wait, service hardware, then acknowledge the PIC themselves. | `kernel/intr/irq.c:4-10`, `kernel/intr/intr.c:83-93` |
| 中断等待 | IRQ wait | Per-task bit-set blocking primitive that wakes when any claimed interrupt line in the mask fires. | `kernel/intr/irq.c:78-94` |
| 恒等映射 | Identity mapping | The 16 MiB supervisor-only low-memory map shared by all directories, allowing kernel code and page tables to run under any active CR3. | `kernel/mm/paging.h:12-16`, `kernel/mm/paging.c:6-22` |
| 物理帧 | Physical frame | One 4 KiB physical page tracked by a kernel bitmap below the 16 MiB ceiling; the only backing store for directories, user pages, and device tables. | `kernel/mm/frame.h:6-17`, `kernel/mm/frame.c:4-17` |
| MMIO 窗口 | MMIO window | A per-task virtual mapping window that maps one physical device page by physical-page number, giving ring 3 drivers stable device addresses. | `kernel/mm/paging.h:24-28`, `kernel/mm/paging.c:52-68` |
| 用户地址空间 | User address space | A page directory that copies the kernel identity PDEs and maps a copied flat image plus one stack frame at `0x40000000`. | `kernel/mm/paging.h:18-22`, `kernel/mm/paging.c:71-104` |
| 内核控制台 | Kernel console | Kernel-owned VGA and COM1 output path reached only through `SYS_WRITE`/`SYS_CLEAR`; user code does not touch ports or VGA memory. | `apps/con.h:4-12`, `kernel/char.c:16-34` |
| 保留输入 | Held input | The one-slot mechanism that keeps an unconsumed keystroke when the tty inbox is full until the timer retries delivery. | `kernel/char.c:46-51`, `kernel/char.c:65-69` |
| 行规程 | Line discipline | The tty service's canonical processing: echo, backspace, newline framing, and buffering completed lines independently of the reader. | `servers/tty/tty.c:10-15`, `servers/tty/tty.c:41-61` |
| TTY 协议 | TTY protocol | The five message kinds for output, line requests, clear, raw input, and mouse events between kernel, shell, and tty. | `servers/tty/tty.h:4-12` |
| 文件协议 | File protocol | The shell-to-fs request/reply messages (`LS`, `OPEN`, `READ`, `CREATE`, `WRITE`, `COMMIT`, `ERR`) over fixed 56-byte data payloads. | `servers/fs/fs.h:8-17` |
| 三段写协议 | Three-phase write | The file creation sequence—create/name, write blocks, commit/publish size—so a partial write never looks like a complete file. | `servers/fs/fsrw.c:6-10`, `servers/fs/fsrw.c:96-103` |
| 三区盘格式 | Three-region disk format | The Pomelo disk layout: one superblock sector, four inode-table sectors, then sequential data blocks. | `servers/fs/format.h:6-25` |
| 零块分配 | Zero-block allocation | The teaching filesystem's free-space convention: an all-zero block is free and is claimed lazily; deletion is unsupported. | `servers/fs/format.h:15-17`, `servers/fs/fsrw.c:14-25` |
| 轮询 PIO | Polled PIO | The ATA driver's programmed-I/O read/write path with yielding status waits; it deliberately avoids interrupts and DMA. | `servers/fs/ata.c:5-14`, `servers/fs/ata.c:30-35` |
| Socket 协议 | Socket protocol | The ten net message kinds that map `open`, `accept`, `recv`, `send`, and `close` operations onto a fixed message transport. | `servers/net/net.h:15-32` |
| Socket 上下文 | Socket context | One of four service-owned records carrying protocol, address pair, TCP state, unacked segment, receive buffer, and parked caller. | `servers/net/sock.c:17-36` |
| 事件泵 | Event pump | The net service's interrupt/message wait loop that drains inbox requests, clears device causes, dispatches frames or timer ticks, then EOIs the owned line. | `servers/net/nic.c:144-148`, `servers/net/net.c:83-95` |
| 用户态驱动 | User-space driver | ATA/e1000 hardware code that runs in its owning ring 3 task with explicit IOPL, port, MMIO, and owned-IRQ access rather than inside the kernel. | `servers/net/net.h:6-11`, `boot/main.c:21-34` |
| 隔离探针 | Isolation probe | A deliberately faulting task that proves a ring 3 touch on kernel memory kills only itself while kernel and other services continue. | `apps/probe/probe.c:6-11` |
| 嵌入式用户镜像 | Embedded user image | A flat ring 3 program linked at `0x40000000`, converted to a binary, and embedded as kernel data; its first four bytes are the entry point. | `tools/user.ld:1-20`, `boot/main.c:11-19` |
| 三区镜像 | Three-region image | The host-side disk image produced from superblock, inode table, and seed data using the same fixed format as the service. | `tools/mkfs/mkfs.c:1-33` |
| QEMU 运行配置 | QEMU runtime configuration | The invocation-only machine/network/port settings—required QEMU, SLIRP user networking, e1000, and host port forwarding—used by `run`, smoke, and nightly. | `CMakeLists.txt:113-120`, `.github/workflows/nightly.yml:44-50` |
