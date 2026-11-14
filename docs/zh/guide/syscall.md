# 06 · 系统调用门

对应代码:`kernel/syscall/`

## 服务为什么需要门

用户态任务想发消息、想阻塞、想重启机器,都必须请内核代办——因为这些事都需要
**内核特权**:写任务表、碰队列、访问硬件。请求内核的通道就是系统调用。

在真微内核里,这条通道由 CPU 特权级 + 跳转门硬件保证。Pomelo 的服务任务
早年全在 ring0,但**接口**从第一天就照做:一个软中断门 `int 0x80`,把“进
内核”这件事形式化下来。[第 14 章](/zh/guide/ring3)任务搬进 ring 3 时,这扇门
加上 `DPL=3` 就成了用户到内核的唯一通道——接口没改,只是门上挂了锁。

## 一张十五行的分发表

```c
enum {
    SYS_YIELD   = 0,   /* 让出 CPU */
    SYS_SEND    = 1,   /* 发送消息 */
    SYS_RECV    = 2,   /* 接收消息(可能阻塞) */
    SYS_IRQ_WAIT= 3,   /* 等待某个 IRQ */
    SYS_REBOOT  = 4,   /* 重启 */
    SYS_WRITE   = 5,   /* ebx=buf, ecx=len: 控制台输出,内核侧 */
    SYS_CLEAR   = 6,   /* 清屏 */
    SYS_SPAWN   = 7,   /* ebx=name: 按名字启动一个已注册程序 */
    SYS_PS      = 8,   /* ebx=buf, ecx=max: 任务表快照 */
    SYS_UPTIME  = 9,   /* 开机秒数:心跳倒回来 */
    SYS_MEM     = 10,  /* ebx=&mem_info: 帧位图,数一遍 */
    SYS_IRQ_ENABLE = 11, /* ebx=irq: 放行 PIC 脚,认领这条线 */
    SYS_V2P     = 12,  /* ebx=va: 虚拟到物理,给 DMA 描述符用 */
    SYS_MMIO    = 13,  /* ebx=pa: 映一页设备寄存器,返回虚拟地址
                        * (16 MiB span 以下的 pa 被拒:那是 RAM,不是设备) */
    SYS_IRQ_EOI = 14,  /* ebx=irq: 重排这条自己认领的线 */
};
```

内核侧按号分派——但**每一个跨过门口的指针都先过校验**。参数走寄存器(eax=号,ebx/ecx=参),返回值写回 eax。校验只有一个助手 `user_ok`:范围必须落在用户区,且它踩到的每一页都必须在**调用者自己**的页目录里映射着;否则一个坏指针崩的是**内核**,而不是递上它的那个任务(`kernel/syscall/syscall.c:26-35`):

```c
static int user_ok(const void *p, u32 len) {
    u32 a = (u32)p, b = a + len;
    if (b < a) return 0;                       /* 回绕 */
    if (a < USER_BASE || b > USER_STACK_TOP) return 0;
    while (a < b) {
        if (!v2p(a)) return 0;                 /* 洞:这一页没映射 */
        a = (a & ~0xFFFu) + 4096;
    }
    return 1;
}
```

```c
void syscall_entry(struct regs *r) {
    switch (r->eax) {
    case SYS_YIELD:
        sched_tick();                 /* 轮转:把 CPU 交给下一位 */
        r->eax = 0;
        return;
    case SYS_SEND:
        if (!user_ok((void *)r->ecx, sizeof(struct msg))) { r->eax = -1; return; }
        r->eax = ipc_send(r->ebx, (struct msg *)r->ecx);
        return;
    case SYS_RECV:
        if (!user_ok((void *)r->ebx, sizeof(struct msg))) { r->eax = -1; return; }
        r->eax = ipc_recv((struct msg *)r->ebx, r->ecx);
        return;
    case SYS_IRQ_WAIT:
        irq_wait(r->ebx);             /* ebx 是 IRQ 掩码 */
        r->eax = 0;
        return;
    case SYS_REBOOT:
        reboot_now();                 /* 往 0x64 口打一拍,重启 */
        return;
    case SYS_WRITE:
        /* 这里的 ecx 是无符号;巨长会在 user_ok 的范围检查上撞回绕而败,
         * 而不是把控制台循环撑爆。 */
        if (!user_ok((const void *)r->ebx, r->ecx)) { r->eax = -1; return; }
        con_write((const char *)r->ebx, r->ecx);   /* 控制台归内核管 */
        r->eax = 0;
        return;
    case SYS_CLEAR:
        con_clear();
        r->eax = 0;
        return;
    case SYS_SPAWN:
        /* sched_spawn 至多读 PROG_NAME 个字节的程序名。 */
        if (!user_ok((const void *)r->ebx, PROG_NAME)) { r->eax = -1; return; }
        r->eax = sched_spawn((const char *)r->ebx);
        return;
    case SYS_PS: {
        /* ps_snapshot 至多写 NTASK-1 行,不管 max 说什么。 */
        u32 want = (u32)r->ecx;
        if (want > NTASK) want = NTASK;
        if (!user_ok((void *)r->ebx, want * sizeof(struct ps_entry))) {
            r->eax = -1;
            return;
        }
        r->eax = ps_snapshot((struct ps_entry *)r->ebx, r->ecx);
        return;
    }
    case SYS_UPTIME:
        r->eax = sched_uptime();
        return;
    case SYS_MEM:
        if (!user_ok((void *)r->ebx, sizeof(struct mem_info))) {
            r->eax = -1;
            return;
        }
        frame_stats((struct mem_info *)r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_ENABLE:
        /* PIC 掩码和 IDT 都是内核地盘,握着线的驱动来这里开口子。
         * 挂上的桩是空的:唤醒人是 irq.c,设备自己的中断状态由驱动
         * 清。认领这个动作把这条线的 EOI 从内核挪给驱动(sys_irq_eoi),
         * 电平触发下得先让设备闭嘴再重排 8259。irq_no_ok 要先来:
         * irq.c 按线号索引自己的表,一个越界的号会把掩码(1u << 16+)
         * 甩出边界,戳到不属于任何人的 PIC 端口。 */
        if (!irq_no_ok(r->ebx)) { r->eax = -1; return; }
        pic_irq_enable(r->ebx);
        irq_install(r->ebx, net_irq_stub);
        irq_claim(r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_EOI:
        /* 认领的另一半:驱动让设备闭了嘴,现在 PIC 才可以重排。内核
         * 投递时故意跳过 EOI,就是为了把顺序留给这里的驱动。少了
         * 同样的范围检查,pic_eoi 会痛快地给一条 16 号以上的线
         * 发 EOI——或者给一次从未触发过的 8-15 从芯片发——把
         * in-service 位钉死在那儿。 */
        if (!irq_no_ok(r->ebx)) { r->eax = -1; return; }
        pic_eoi(r->ebx);
        r->eax = 0;
        return;
    case SYS_V2P:
        r->eax = v2p(r->ebx);
        return;
    case SYS_MMIO:
        r->eax = mmio_map(r->ebx);      /* 拒绝 16 MiB span 以下的 pa */
        return;
    }
    r->eax = -1;                      /* 没这个号 */
}
```
```

## 用户态的调用约定

服务任务不能直接调 `ipc_send`,而是通过一个小包装,把参数塞进寄存器、触发 `int 0x80`:

```c
static inline int sys_send(int dst, struct msg *m) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_SEND), "b"(dst), "c"(m)
        : "memory");
    return ret;
}
```

`int 0x80` 让 CPU 转到 IDT 第 128 项——我们在 `idt_init` 里把它接到
`intr_stub_128`。桩补一个假的错误码、压上向量号,跳进 `intr_common` 保存
`regs` 现场,交给 `intr_dispatch`;它认出 0x80 才转手 `syscall_entry`。
这条路径和[中断章](/zh/guide/interrupts)用的是同一套桩机制,只是入口不同。

## 一扇门,而不是一堆函数

值得强调:Pomelo 的任务和内核之间**只有这十五个调用**。读文件、按键,统统
不经过系统调用,而是服务与服务之间的消息(走 `SYS_SEND`/`SYS_RECV`);
`SYS_WRITE`/`SYS_CLEAR` 是仅有的例外——屏幕归内核管,任务想说话只有这一个
出口(缘由见[第 14 章](/zh/guide/ring3))。最新的四个(11–14)是网卡驱动住进
ring 3 时添的:开中断线、确认收讫、DMA 描述符的物理地址、设备寄存器的映射——
驱动要的特权,内核一概不白给,一次一个门。系统调用表越小,内核越小;内核越小,
越接近微内核的本来面目。

## 本章文件

```
kernel/syscall/syscall.h   调用号与用户态 inline 包装
kernel/syscall/syscall.c   int 0x80 入口与分发表
```

下一章进入用户态世界:[磁盘格式](/zh/guide/disk-format)。
