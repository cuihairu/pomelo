# 06 · 系统调用门

对应代码:`kernel/syscall/`

## 服务为什么需要门

用户态任务想发消息、想阻塞、想重启机器,都必须请内核代办——因为这些事都需要
**内核特权**:写任务表、碰队列、访问硬件。请求内核的通道就是系统调用。

在真微内核里,这条通道由 CPU 特权级 + 跳转门硬件保证。Pomelo 里没有特权级
(全是 ring0),但**接口**照做:一个软中断门 `int 0x80`,把“进内核”这件事
形式化下来。日后升级到 ring3 时,这一层不用改。

## 一张五行的分发表

```c
enum {
    SYS_YIELD   = 0,   /* 让出 CPU */
    SYS_SEND    = 1,   /* 发送消息 */
    SYS_RECV    = 2,   /* 接收消息(可能阻塞) */
    SYS_IRQ_WAIT= 3,   /* 等待某个 IRQ */
    SYS_REBOOT  = 4,   /* 重启 */
};
```

内核侧就是按号分派。参数走寄存器(eax=号,ebx/ecx=参),返回值写回 eax:

```c
void syscall_entry(struct regs *r) {
    switch (r->eax) {
    case SYS_YIELD:
        sched_tick();                 /* 轮转:把 CPU 交给下一位 */
        r->eax = 0;
        return;
    case SYS_SEND:
        r->eax = ipc_send(r->ebx, (struct msg *)r->ecx);
        return;
    case SYS_RECV:
        r->eax = ipc_recv((struct msg *)r->ebx, r->ecx);
        return;
    case SYS_IRQ_WAIT:
        irq_wait(r->ebx);             /* ebx 是 IRQ 掩码 */
        r->eax = 0;
        return;
    case SYS_REBOOT:
        reboot_now();                 /* 往 0x64 口打一拍,重启 */
    }
    r->eax = -1;                      /* 没这个号 */
}
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
`syscall_entry`,后者保存现场后调用 `syscall_dispatch`。这条路径和[中断章](/guide/interrupts)
用的是同一套桩机制,只是入口不同。

## 一扇门,而不是一堆函数

值得强调:Pomelo 的服务和内核之间**只有这五个调用**。读文件、打印、按键,统统
不经过系统调用,而是服务与服务之间的消息(走 `SYS_SEND`/`SYS_RECV`)。系统调用
表越小,内核越小;内核越小,越接近微内核的本来面目。

## 本章文件

```
kernel/syscall/syscall.h   调用号与用户态 inline 包装
kernel/syscall/syscall.c   int 0x80 入口与分发表
```

下一章进入用户态世界:[磁盘格式](/guide/disk-format)。
