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

`stubs.S` 里为每个向量各写一个几行的桩,统一压入向量号再跳进公共入口:

```asm
stub_irq0:
    pushl $32                 # 时钟是 IRQ0,统一排到 32 号
    jmp   intr_common
```

`intr_common` 把现场保存成 `regs` 结构,交给 C 的分发函数。

## 分发:先应答,再唤醒

```c
void intr_dispatch(struct regs *r) {
    u32 irq = r->int_no - 32;         /* 32 号以上才是硬件中断 */
    if (irq < 16) {
        pic_eoi(irq);                 /* 先告诉 8259:这个我收下了 */
        irq_raise(irq);               /* 再唤醒在等着它的任务 */
    }
}
```

顺序很关键:**必须先把 EOI 发给 8259**,否则这颗芯片会一直压着后续中断不发。
这是硬件层级的老式“流控”。

再解释一下 PIC 重映射(`pic_remap`):默认 IRQ0–15 映射到 0–15 号,会和 CPU 异常
(0–31 号)撞车。我们把它们整体挪到 32–47 号,井水不犯河水。

## 小机制:irq_wait,把中断变成“可等待的事件”

驱动不该在中断处理函数里干活。真正的做法是:中断只做“唤醒”,干活的任务在别处等。
内核提供原语:

```c
void irq_wait(u32 irq);   /* 阻塞,直到 8259 报告该 IRQ */
```

tty 服务就靠它等键盘。这样中断处理函数短到极致,而“等按键”在用户态服务眼里
就是一次普通的阻塞调用——微内核里,中断也被抽象成了可等待的对象。

## 本章文件

```
kernel/intr/intr.h     IRQ 原语与 regs 结构
kernel/intr/idt.c      IDT 填充与加载
kernel/intr/stubs.S    256 个中断桩
kernel/intr/intr.c     pic_remap / intr_dispatch / irq_wait / irq_raise
```

下一章:[调度](/guide/scheduling)。
