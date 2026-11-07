# 术语表

本表收录在源码中反复出现的架构级词汇,保持精简并始终指向实现证据。English version: [Glossary](/guide/glossary).

| 术语 | 英文 | 定义 | file:line |
| --- | --- | --- | --- |
| 多重引导 | Multiboot | 让 QEMU 直接内核加载器进入 32-bit 入口的引导期 magic/header 契约。 | `boot/boot.S:1-30` |
| 全局描述符表 | Global Descriptor Table (GDT) | 六项平坦描述符表,建立 ring 0/ring 3 段以及进入 ring 0 时使用的 TSS。 | `boot/gdt.c:18-25` |
| 任务状态段 | Task State Segment (TSS) | CPU 拥有的转换记录;每次切换用户任务前,其 `esp0` 被设为当前任务内核栈顶。 | `boot/gdt.c:27-40` |
| 程序注册表 | Program registry | 编译期按名字索引的嵌入用户镜像及其出生 `EFLAGS` 清单,也是唯一的 spawn 能力。 | `kernel/sched/sched.h:20-30`, `kernel/sched/prog.c:13-23` |
| 任务座位 | Task seat | 固定任务表中的一个槽位,保存内核 ESP、页目录、状态、唤醒标志、程序身份和收件箱。 | `kernel/sched/sched.h:38-48` |
| 稳定任务标识 | Stable task ID | 按名重启优先选择该程序旧座位的不变量,使既有 `TID_*` 目的地继续有效。 | `kernel/sched/prog.c:25-48` |
| 轮转抢占 | Round-robin preemption | 100 Hz PIT 心跳选择下一个 ready 座位;yield 复用交接但不推进内核时钟。 | `kernel/sched/sched.c:57-103` |
| 内核消息 | Kernel message | 复制进固定 16 槽环形收件箱的 72 字节值;内核填充 `src`,中断路径以签名为 0 的来源发送。 | `kernel/ipc/ipc.h:6-22`, `kernel/ipc/ipc.c:29-37` |
| 收件箱 | Inbox | 每任务的定长消息环,可原子地入队、阻塞接收者并唤醒。 | `kernel/ipc/ipc.h:19-22`, `kernel/ipc/ipc.c:13-27` |
| 等待发送 | Send-wait | 客户端在目标收件箱满时 yield 并重试的循环,避免请求静默丢失或回复悬挂。 | `kernel/syscall/syscall.h:62-68` |
| 系统调用门 | Syscall gate | 唯一 DPL 3 中断门(`0x80`),ring 3 只能通过它进入内核服务分发。 | `kernel/intr/idt.c:61-63`, `kernel/syscall/syscall.h:39-44` |
| 用户指针校验 | User pointer validation | 系统调用边界检查:地址区间必须在用户区,且每个页都在调用者页目录中映射。 | `kernel/syscall/syscall.c:21-35` |
| 中断所有权 | IRQ ownership | 区分内核 handler(立即应答)与 ring 3 claimer(等待、服务硬件后自行应答 PIC)。 | `kernel/intr/irq.c:4-10`, `kernel/intr/intr.c:83-93` |
| 中断等待 | IRQ wait | 每任务位集阻塞原语;掩码中任意已声明中断线触发即唤醒。 | `kernel/intr/irq.c:78-94` |
| 恒等映射 | Identity mapping | 所有页目录共享的 16 MiB supervisor-only 低内存映射,使内核代码和页表在任意 CR3 下可达。 | `kernel/mm/paging.h:12-16`, `kernel/mm/paging.c:6-22` |
| 物理帧 | Physical frame | 16 MiB 上限以下由内核位图跟踪的一个 4 KiB 物理页;是页目录、用户页和设备表的唯一后备。 | `kernel/mm/frame.h:6-17`, `kernel/mm/frame.c:4-17` |
| MMIO 窗口 | MMIO window | 按物理页号映射物理设备页的每任务虚拟窗口,给 ring 3 驱动稳定设备地址。 | `kernel/mm/paging.h:24-28`, `kernel/mm/paging.c:52-68` |
| 用户地址空间 | User address space | 复制内核恒等 PDE,并在 `0x40000000` 映射一份复制镜像和一个栈帧的页目录。 | `kernel/mm/paging.h:18-22`, `kernel/mm/paging.c:71-104` |
| 内核控制台 | Kernel console | 只能经 `SYS_WRITE`/`SYS_CLEAR` 到达的 VGA 与 COM1 输出路径;用户代码不碰端口或 VGA 内存。 | `apps/con.h:4-12`, `kernel/char.c:16-34` |
| 保留输入 | Held input | tty 收件箱满时保存未消费按键的一个槽位,直到 timer 重试投递。 | `kernel/char.c:46-51`, `kernel/char.c:65-69` |
| 行规程 | Line discipline | tty 服务的规范处理:回显、退格、换行定帧,以及与读者速度解耦的完成行缓冲。 | `servers/tty/tty.c:10-15`, `servers/tty/tty.c:41-61` |
| TTY 协议 | TTY protocol | 内核、shell 与 tty 之间用于输出、行请求、清屏、原始输入和鼠标事件的五种消息。 | `servers/tty/tty.h:4-12` |
| 文件协议 | File protocol | shell 到 fs 的请求/回复消息(`LS`, `OPEN`, `READ`, `CREATE`, `WRITE`, `COMMIT`, `ERR`),载荷固定 56 字节。 | `servers/fs/fs.h:8-17` |
| 三段写协议 | Three-phase write | 文件创建序列——create/name、write blocks、commit/publish size,使部分写入不会看起来像完整文件。 | `servers/fs/fsrw.c:6-10`, `servers/fs/fsrw.c:96-103` |
| 三区盘格式 | Three-region disk format | Pomelo 磁盘布局:1 个超级块扇区、4 个 inode 表扇区,之后是顺序数据块。 | `servers/fs/format.h:6-25` |
| 零块分配 | Zero-block allocation | 教学 FS 的空闲空间约定:全零块视为空闲并懒分配;不支持删除。 | `servers/fs/format.h:15-17`, `servers/fs/fsrw.c:14-25` |
| 轮询 PIO | Polled PIO | ATA 驱动的 programmed-I/O 读写路径,状态等待期间让出 CPU;刻意不用中断和 DMA。 | `servers/fs/ata.c:5-14`, `servers/fs/ata.c:30-35` |
| Socket 协议 | Socket protocol | 十种 net 消息,把 `open`, `accept`, `recv`, `send`, `close` 操作映射到定长消息传输。 | `servers/net/net.h:15-32` |
| Socket 上下文 | Socket context | 四项服务端记录之一,携带协议、地址对、TCP 状态、未确认段、接收缓冲和挂起调用者。 | `servers/net/net.c:437-470` |
| 事件泵 | Event pump | net 服务的中断/消息等待循环:先清空请求,读取设备原因,再按来源分发帧或 timer tick,最后 EOI 所有权中断线。 | `servers/net/net.c:148-174`, `servers/net/net.c:1060-1077` |
| 用户态驱动 | User-space driver | 运行在拥有它的 ring 3 任务中的 ATA/e1000 硬件代码,通过显式 IOPL、端口、MMIO 和所有权 IRQ 访问设备。 | `servers/net/net.h:6-11`, `boot/main.c:21-34` |
| 隔离探针 | Isolation probe | 刻意触发缺页的任务,证明 ring 3 触碰内核内存只会杀死自己,内核与其他服务继续运行。 | `apps/probe/probe.c:6-11` |
| 嵌入式用户镜像 | Embedded user image | 链接在 `0x40000000`、转换成扁平二进制并嵌入内核数据的 ring 3 程序;前四字节是入口点。 | `tools/user.ld:1-20`, `boot/main.c:11-19` |
| 三区镜像 | Three-region image | 宿主侧由超级块、inode 表和种子数据生成的磁盘镜像,与服务端使用同一固定格式。 | `tools/mkfs/mkfs.c:1-33` |
| QEMU 运行配置 | QEMU runtime configuration | 仅由调用参数表达的机型/网络/端口设置——必需 QEMU、SLIRP user networking、e1000 和宿主端口转发——供 `run`、smoke 与 nightly 使用。 | `CMakeLists.txt:113-120`, `.github/workflows/nightly.yml:44-50` |
