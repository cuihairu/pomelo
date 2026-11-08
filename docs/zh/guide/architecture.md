# 架构分层

这张图按职责而非目录名切分源码。它与 [导论](/zh/guide/intro) 的章节顺序互补;[术语表](/zh/guide/glossary) 使用同一组源码证据。

## 分层总览

| # | 抽象层 | 职责 | 代表模块 |
| --- | --- | --- | --- |
| 1 | 引导与平台 | 进入 C、装载特权与中断表、编程定时器与中断控制器,并按注册表启动用户程序。 | `boot/boot.S`, `boot/gdt.c`, `boot/main.c` |
| 2 | 内核机制 | 只保留五个所有任务都绕不开的机制:中断分发、分页、调度、IPC、系统调用门。 | `kernel/intr`, `kernel/mm`, `kernel/sched`, `kernel/ipc`, `kernel/syscall` |
| 3 | 内核专属输入输出 | 解码 IRQ1/IRQ4/IRQ12 硬件事件,渲染控制台输出,并把硬件事件发布为定长消息。 | `kernel/char.c`, `kernel/mouse.c`, `kernel/vga.c` |
| 4 | 用户态服务 | 在独立 ring 3 地址空间内持有设备驱动和状态,通过命名请求/回复消息协议对外服务。 | `servers/fs`, `servers/tty`, `servers/net` |
| 5 | 应用 | 把行、命令和回复转换为服务协议或系统调用。 | `apps/shell`, `apps/netsh`, `apps/probe` |
| 6 | 构建与打包 | 链接内核与 ring 3 程序,把用户镜像嵌入内核,并生成磁盘镜像。 | `CMakeLists.txt`, `tools/user.ld`, `tools/mkfs` |
| 7 | 验证与配置 | 启动 QEMU、驱动串口与 PS/2 输入、检查协议可见结果;提供运行期机型、网络和端口输入。 | `tools/smoke*.sh`, `tools/walkthrough.sh`, `CMakeLists.txt`, `.github/workflows/*.yml` |

## 引导与平台

`boot/boot.S` 保存多重引导头和首个 32-bit 入口;`boot/gdt.c` 覆盖遗留描述符,建立平坦的 ring 0/ring 3 段和 TSS;`boot/main.c` 按依赖顺序初始化平台,嵌入六个用户镜像,带着能力标志注册它们,按固定任务序按名 spawn,最后把 CPU 交给 `TID_FS`。配置输入是 `boot/kernel.ld` 的内核 ELF 链接布局和 `boot/main.c` 中的编译期注册表。

**文件:** `boot/boot.S`, `boot/gdt.c`, `boot/gdt.h`, `boot/main.c`, `boot/kernel.ld`。

## 内核机制

中断分发持有 IDT、PIC remap、PIT、异常和分发路径,该路径要么进入系统调用表,要么把 IRQ 交给所有者;分页持有 16 MiB 内核恒等映射、物理帧位图、用户页目录、MMIO 与虚拟/物理翻译;调度持有任务座位、ring 3 启动帧、程序注册表、抢占、阻塞和按名重启;IPC 持有每个任务的定长收件箱;系统调用门校验用户指针并暴露 15 个系统操作。状态保存在内核数组中:任务/收件箱记录、物理帧位图、程序注册表、IDT、中断所有权表和页表。共享 ABI 位于 `kernel/syscall/syscall.h`;没有运行期配置文件。

**文件:** `kernel/intr/intr.h`, `kernel/intr/idt.c`, `kernel/intr/irq.c`, `kernel/intr/intr.c`, `kernel/intr/stubs.S`;`kernel/mm/paging.h`, `kernel/mm/paging.c`, `kernel/mm/frame.h`, `kernel/mm/frame.c`;`kernel/sched/sched.h`, `kernel/sched/sched.c`, `kernel/sched/prog.c`, `kernel/sched/switch.S`;`kernel/ipc/ipc.h`, `kernel/ipc/ipc.c`;`kernel/syscall/syscall.h`, `kernel/syscall/syscall.c`。

## 内核专属输入输出

这一层就是平台控制台(platform console):控制台硬件策略(`kernel/char.c`、`kernel/mouse.c`、`kernel/vga.c`),刻意不算五个内核机制之一。输入必须在恒等映射的内核世界运行,因此 IRQ1/IRQ4 与 IRQ12 位于这里;它们解码键盘、串口与鼠标样本,并以定长消息发送给 tty 服务。输出通过 `SYS_WRITE` 和 `SYS_CLEAR` 进入,内核侧写 VGA 和 COM1。这是刻意例外:启动期持有的输入硬件是唯一留在内核里的硬件。状态包括控制台光标、UART 配置、一个按键保留槽、鼠标包状态与光标保存单元。协议名在 tty 头文件;用户可见文案由内核代码或调用应用发出。

**文件:** `kernel/char.h`, `kernel/char.c`, `kernel/mouse.h`, `kernel/mouse.c`, `kernel/vga.h`, `kernel/vga.c`, `servers/tty/tty.h`。

## 用户态服务

fs 服务持有 ATA PIO 和三区磁盘格式,启动时载入超级块与 inode 表,应答 shell 文件请求,并在缺盘或坏 magic 时降级为明确拒绝。tty 服务持有行规程和未认领的完成行。net 服务持有 PCI/e1000/MMIO/DMA、ARP、IP、ICMP、UDP、极简 TCP 和四项 socket 表。三者都运行在 ring 3,只通过 `SYS_SEND`/`SYS_RECV` 通信。服务协议 schema 是头文件(`servers/fs/fs.h`, `servers/tty/tty.h`, `servers/net/net.h`);磁盘 schema 是 `servers/fs/format.h`。运行期网络参数是编译期 SLIRP 约定,而非配置文件。

**文件:** `servers/fs/format.h`, `servers/fs/fs.h`, `servers/fs/fs.c`, `servers/fs/fsrw.c`, `servers/fs/ata.c`;`servers/tty/tty.h`, `servers/tty/tty.c`;`servers/net/net.h`, `servers/net/net.c`, `servers/net/nic.c`, `servers/net/arp.c`, `servers/net/ip.c`, `servers/net/sock.c`。

## 应用

shell 是 REPL 与客户端层:它有命令表,并为 tty 和 fs 协议提供薄包装。`netsh` 是事件循环,打开并驱动 socket 上下文完成回显与问候。`probe` 发送一条 tty 消息后刻意触碰内核内存,以证明隔离。配置输入是编译期客户端表和网络常量(`apps/shell/shell.c`, `apps/shell/shell.h`, `apps/netsh/netsh.c`);协议头通过源码包含共享,没有独立客户端协议文件。

**文件:** `apps/shell/shell.h`, `apps/shell/shell.c`, `apps/shell/fsio.c`, `apps/shell/cmd_*.c`, `apps/netsh/netsh.c`, `apps/probe/probe.c`, `apps/con.h`。

## 构建与打包

顶层构建链接六个自由实现的扁平 ring 3 镜像,转换成二进制,包装为可重定位对象并嵌入内核;`tools/user.ld` 把入口点放在开头,并把 `.bss` 折叠进 `.data`。内核由 `boot/kernel.ld` 链接;`mkfs` 用 `tools/mkfs/seed` 生成磁盘镜像。构建输入是 `CMakeLists.txt`、链接脚本和种子文件;没有运行期配置 schema。

**文件:** `CMakeLists.txt`, `tools/user.ld`, `boot/kernel.ld`, `tools/mkfs/mkfs.c`, `tools/mkfs/seed/*`。

## 验证与配置

smoke 与 walkthrough 脚本启动 QEMU,驱动串口、PS/2、鼠标、网络、清屏和重启路径,然后断言日志或 VGA 快照。CI 重跑同一批测试,增加对可宿主链接服务路径的单元测试,并发布文档。运行期配置是调用形状:QEMU 必需,`run` 选择 SLIRP 网络与 e1000 设备,smoke 脚本转发宿主 TCP/UDP 端口;没有配置解析器或 schema 文件。

**文件:** `tools/smoke.sh`, `tools/smoke-ps2.sh`, `tools/smoke-mouse.sh`, `tools/smoke-net.sh`, `tools/smoke-clear.sh`, `tools/smoke-reboot.sh`, `tools/walkthrough.sh`, `tools/nightly-run.sh`, `tests/unit/test_fs.c`, `tests/unit/test_net.c`, `tests/unit/test_mkfs.sh`, `CMakeLists.txt`, `.github/workflows/*.yml`。

## 拆分理由

1. **引导区别于机制与服务。** `boot/main.c` 必须在开中断前调用 GDT、IDT、PIC、PIT、分页和 spawn 初始化;这是时间排序职责,不是可复用内核策略(`boot/main.c:44-62`)。
2. **内核意味着普遍,不只是特权。** 源码明确只有每个任务都绕不开的事才进内核:中断分发、调度、IPC、系统调用门和分页;文件服务不在任何任务的最小路径上(`docs/guide/intro.md:9-17`,实现镜像于 `kernel/intr`、`kernel/sched`、`kernel/ipc`、`kernel/syscall`、`kernel/mm`)。
3. **中断控制与中断所有权分离。** `intr.c` 编程 PIC/PIT 并路由向量,`irq.c` 安装 handler、记录 claimer 与 waiter,并允许 ring 3 驱动在安抚设备后再 EOI(`kernel/intr/intr.c:70-94`, `kernel/intr/irq.c:4-10`)。
4. **能力表区别于调度。** `sched.c` 启动已准备好的座位,`prog.c` 持有编译期“名字到镜像”注册表并优先复用旧座位;启动与恢复共用同一扇门,而 CPU 交接不与程序查找耦合(`kernel/sched/sched.c:31-55`, `kernel/sched/prog.c:29-48`)。
5. **IPC 是独立机制,不嵌进调度器。** 调度器只知道座位是否 ready,IPC 持有定长消息形状、容量、复制、阻塞和唤醒;分离后所有服务协议都能复用同一传输(`kernel/ipc/ipc.c:13-63`, `kernel/sched/sched.h:38-48`)。
6. **控制台输入归内核,控制台状态归服务。** 键盘和串口 ISR 必须存在于恒等映射内核世界,但退格、回显和完成行缓冲是终端策略,因此属于 tty(`kernel/char.c:7-14`, `servers/tty/tty.c:10-15`)。
7. **文件协议与磁盘格式区别于块 I/O。** `fs.h` 的消息 API、`format.h` 的盘上超级块/inode schema、`fs.c` 的服务分发、`ata.c` 的轮询 PIO 驱动各有变化速率和信任边界(`servers/fs/fs.h:8-27`, `servers/fs/format.h:6-48`, `servers/fs/ata.c:5-14`)。
8. **网络传输与 socket 服务状态模型不同。** 以太网/IP/协议帧由流量驱动,socket 上下文承载挂起的应用请求、序列号、接收缓冲和重传状态;混在一起会让服务 API 依赖线缆帧格式(`servers/net/ip.c:63-88`, `servers/net/sock.c:17-36`)。
9. **应用保持薄客户端。** shell 明确只讲 tty 和 fs 协议,netsh 的运行期世界就是 net 服务回复;这把命令/UI 策略留在服务状态机之外(`apps/shell/shell.h:9-10`, `apps/netsh/netsh.c:163-174`)。
10. **构建与运行期分层。** blob 管线和磁盘镜像只服务于引导与 smoke 前,内核/服务在其后承担行为;CMake blob 管线和 mkfs 是宿主工具,不是内核机制(`CMakeLists.txt:11-48`, `tools/mkfs/mkfs.c:1-4`)。
11. **验证驱动外部契约。** smoke 测试调用实际 QEMU 命令面并检查协议可见结果,因此测的是配置与行为,而不是内部函数(`CMakeLists.txt:113-153`, `tools/smoke-net.sh:18-25`)。
