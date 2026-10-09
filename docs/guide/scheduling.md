# 04 · 调度

对应代码:`kernel/sched/`

## 任务就是一个栈

进程、线程这些词容易唬人。剥开看,一个可运行的任务就三样东西:**一段代码、
一个栈、一份保存好的寄存器现场**。Pomelo 的任务表很小:

```c
struct task {
    u32        esp;       /* 现场保存点:swap 时把 esp 存在这 */
    u32        pdir;      /* 页目录,0 = 用内核自己的;第 14 章 */
    int        state;     /* FREE / READY / BLOCKED */
    int        woke;      /* 被 sched_wake 碰过;irq_wait 靠它认出“为消息而醒” */
    struct msgq inbox;    /* 它自己的消息队列,IPC 章详解 */
};
```

栈是从一张静态大数组里切出来的(每任务 4 KB)——教学内核不搞分配器,连栈都
先量好尺寸。

`esp` 是全部秘密所在。所谓“切换”,就是**换掉 esp 再 `ret`**——CPU 就会用另一个
栈的现场继续跑。`pdir` 是另一半:切换时除了换栈,还要换 cr3,任务之间地址空间
跟着走(第 14 章详解)。

## 上下文切换:一段不得不看的汇编

`kernel/sched/switch.S` 是整个调度器的心脏,不到二十行:

```asm
switch_to:
    cli                    # 换栈中途被时钟打断,两个栈都得坏
    movl  4(%esp), %eax    # &old->esp
    movl  8(%esp), %edx    # new->esp
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    movl  %esp, (%eax)     # 把当前 esp 存进 old->esp
    movl  %edx, %esp       # 换成新任务的栈
    popl  %ebx
    popl  %esi
    popl  %edi
    popl  %ebp
    ret                    # 用新任务的返回地址继续
```

一次调用把**被调用者要保存的寄存器**压栈、换栈、再弹回——从新栈里 `ret` 出去
时,CPU 已经在跑另一个任务了。C 编译器保证的 callee-saved 寄存器恰好这四个,
压它们就够了。`cli` 关中断护住换栈的几条指令;全新任务的第一次 `ret` 落在
`ring3_entry` 上:设好用户数据段,然后 `iret` 直接落进 ring 3——这个世界里
没有 ring 0 的任务可进(第 14 章)。

## 何时切

Pomelo 是**协作 + 抢占**的混合体,但主动作是抢占:

- **时钟抢占**:每次 IRQ0,调度器决定要不要切;
- **主动让出**:`yield()`(系统调用 `SYS_YIELD`)让正在忙等的任务换人。

真正的决定在 `sched_tick`:

```c
void sched_tick(void) {
    if (cur < 0) return;
    int next = pick_ready();          /* cur 之后第一个 READY 的任务 */
    if (next >= 0) sched_switch(next);
}
```

朴素轮转(round-robin):从当前任务往后找第一个没阻塞的。10 ms 一轮,
谁也别想独占 CPU。

## 阻塞:让出 CPU 的正当理由

一个任务在等 IPC 消息或等中断时,把状态置 `BLOCKED`,再调 `sched_next` 让出。
调度器会跳过它:

```c
void sched_wake(int tid) {
    if (tasks[tid].state == ST_BLOCKED) {
        tasks[tid].state = ST_READY;
        tasks[tid].woke = 1;          /* 告诉 irq_wait:是我叫的你 */
    }
}
```

`irq_raise` 和 `ipc_send` 干的就是 `sched_wake` 这件事——**中断和消息都能唤醒任务**。
这是整本书反复出现的同一招。

## 没人可跑:去睡觉,别空转

如果所有任务都 BLOCKED 了呢?这不是死锁——每个服务都正等着自己的硬件。
`sched_next` 会把 CPU 停进 `hlt`:中断一开,CPU 睡到下一个中断(最不济是
100 Hz 的时钟)再叫人。还有一条细腻的出路:如果时钟刚好叫醒的就是正在做
“阻塞自己”这件事的任务,`sched_next` 发现场上已是 READY,直接返回——阻塞
当场作废。判断、入睡、复检,必须关中断一口气做完。

## 本章文件

```
kernel/sched/sched.h    task 结构、任务表与 API
kernel/sched/sched.c    占座建任务、轮转、阻塞/唤醒
kernel/sched/switch.S   switch_to 寄存器换栈
kernel/sched/prog.c     程序注册表、sched_spawn、ps_snapshot(第 15 章)
```

下一章:[IPC](/guide/ipc)。
