# 03 · 中断分发

对应代码:`kernel/intr/`

## 中断是内核存在的理由

没有中断,内核只是一段被调用一次就睡着的代码。中断把“外面出事了”这件事塞进 CPU,
内核才有机会反应。Pomelo 的内核只有两件事和中断直接相关:**确认源头**和**把事件
交给等在它上面的任务**。

## IDT:一张 256 项的门牌表

CPU 收到中断号 `n`,就查 IDT 的第 `n` 项,跳到对应的处理入口。我们只需要两种门:
异常(除零、页错误)和硬件中断。`kernel/intr/idt.c` 把每一项都填成一个汇编桩:

```c
void idt_set(int n, u32 handler, u16 sel, u8 flags) {
    idt[n].offset_lo = handler & 0xffff;
    idt[n].selector  = sel;
    idt[n].zero      = 0;
    idt[n].flags     = flags;          /* 0x8E = 存在的 32 位中断门 */
    idt[n].offset_hi = handler >> 16;
}
```

`stubs.S` 里为每个**实际使用**的向量各写一个几行的桩,统一压入向量号再跳进公共入口:

```asm
.macro STUB vec
intr_stub_\vec:
    pushl $0                  # 有的异常自带错误码,普通中断补 0 对齐
    pushl $\vec
    jmp   intr_common
.endm

STUB 32                       # 时钟(PIT)
STUB 33                       # 键盘
STUB 36                       # 串口
STUB 44                       # 鼠标(IRQ12)
STUB 128                      # 系统调用门

# 13 和 14 由 CPU 推错误码,桩里不再补 0(见下文)
intr_stub_13:
    pushl $13
    jmp   intr_common
intr_stub_14:
    pushl $14
    jmp   intr_common
```

CPU 异常(0–31 号)大多不逐个解码:内核态遇到异常就意味着出了大错,共用
一个桩,打一行 PANIC 然后停机。两个例外是 13(general protection)和
14(page fault)——它们自带错误码,而且可能是 ring 3 任务惹的祸,得逐个
查明再决定杀谁,见[第 14 章](/guide/ring3)。`intr_common` 把现场保存成
`regs` 结构,交给 C 的分发函数。

## 分发:先应答,再唤醒

```c
void intr_dispatch(struct regs *r) {
    if (r->int_no == 0x80) {          /* 0x80 号 = 系统调用门 */
        syscall_entry(r);
        return;
    }
    if (r->int_no == 14) {             /* 页错误 */
        task_fault(r, "page fault", cr2_fault());
        return;
    }
    if (r->int_no == 13) {             /* general protection */
        task_fault(r, "general protection fault", 0);
        return;
    }
    if (r->int_no >= 32 && r->int_no < 48) {
        u32 irq = r->int_no - 32;
        irq_dispatch(irq);            /* 认领了这条线的,先跑它的三行 */
        if (!irq_owned(irq))
            pic_eoi(irq);             /* 内核自己的线:当场收下 */
        irq_raise(irq);               /* 再唤醒在等着它的任务 */
        if (irq == IRQ_PIT) {
            char_retry();             /* tty 队列没收下的输入,再试一次 */
            sched_clock_tick();       /* 心跳 + 抢占 */
        }
    }
}
```

EOI 的归属跟着线的归属走:键盘、鼠标、时钟这些内核自留的线,内核当场
EOI;用户态驱动认领的线(网卡那种),内核**故意不发**——电平触发下得先
让设备闭嘴再重新武装 8259,而只有握着硬件的驱动知道怎么让它闭嘴,驱动
清完设备再走 `sys_irq_eoi`。顺序要是颠倒,8259 会立刻把同一条电平重新
递上来,一个中断变成一场风暴。

再解释一下 PIC 重映射(`pic_remap`):默认 IRQ0–15 映射到 0–15 号,会和 CPU 异常
(0–31 号)撞车。我们把它们整体挪到 32–47 号,井水不犯河水。

`pic_remap` 还有一手:只打开**有人认领**的线(时钟、键盘、串口、级联),其余全部
屏蔽。这不是洁癖——一条敞着没人管的中断线,信号一到就会穿过空的 IDT 门,直接
#GP 掀翻内核。调磁盘驱动时吃过这个亏:IRQ14 的第一个命令刚完成,系统就进了
异常,错误码 `0x172` = 46×8+2,正是“空门”的签名。

## 小机制:irq_wait,把中断变成“可等待的事件”

驱动不该在中断处理函数里干活。真正的做法是:中断只做“唤醒”,干活的任务在别处等。
内核提供原语:

```c
void irq_wait(u32 mask);   /* 阻塞,直到 mask 里任一 IRQ 报告 */
```

如今的控制台没有用它(键盘和串口的中断在 kernel/char.c 里被直接变成消息投给
tty,见[第 9 章](/guide/tty-server)),这个原语留给下一个写驱动的你。它有两
个提前返回的出口:消息到了(调用方得去处理收件箱),或者——收件箱里**现在
就有没读的消息**。第二条是血泪换来的:一个任务若在“清空收件箱”和“入睡”两步
之间被时钟打断,醒来后会带着没读的信睡死过去,发信人只能干等。检查与入睡必须
关中断一口气做完——**永远不要带着没读的信入睡**。这样中断处理函数里没有多余逻辑,
“等设备”在服务眼里就是一次普通的阻塞调用——微内核里,中断也被抽象成了可
等待的对象。

## 本章文件

```
kernel/intr/intr.h     IRQ 原语与 regs 结构
kernel/intr/idt.c      IDT 填充与加载
kernel/intr/stubs.S    实际使用的向量各一个桩
kernel/intr/intr.c     pic_remap / pit_init / intr_dispatch / panic
kernel/intr/irq.c      认领表、待决位与 irq_wait
```

下一章:[调度](/guide/scheduling)。
