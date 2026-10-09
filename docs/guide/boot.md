# 02 · 启动

对应代码:`boot/`

## 上电之后发生了什么

CPU 复位后停在实模式,BIOS 做完自检,把控制权交给引导器。我们不想自己写引导器,
也不想做 ISO,于是借用一个约定:**Multiboot**。QEMU 的 `-kernel` 能识别带
Multiboot 头的 ELF 文件,自己充当引导器:把内核载入内存,切到保护模式,
再跳到我们指定的入口。

这个约定只需要内核开头有 8 个字节的魔数和对齐。看 `boot/boot.S`:

```asm
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x0            # 内核由引导器载入,无需请求额外内存信息
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECK
```

`CHECK` 是前两者的补码和,和必须为 0——这是 Multiboot 规定的完整性校验。
`boot/kernel.ld` 把这个段放到最前面,保证魔数落在文件开头。

## 从汇编跳进 C

引导器切进保护模式后,栈是指望不上的,得自己设:

```asm
_start:
    movl $stack_top, %esp     # 设栈,否则 C 函数一返回就崩
    pushl %ebx                # Multiboot 信息结构指针,转交 kmain
    call  kmain
1:  cli
    hlt
    jmp 1b                    # 内核入口不该返回;真返回了就停住
```

栈空间开在 `.bss` 里的一块静态数组——此刻还没有内存管理,能用静态内存就用静态内存。

## 第一行 C

`boot/main.c` 里的 `kmain` 是整条启动链的收束口,顺序不能乱:

```c
void kmain(unsigned magic, unsigned info) {
    (void)info;
    console_init();          /* 1. 先有个能打字的屏幕 */
    kprintf("pomelo booting (magic=%x)\n", magic);

    gdt_init();              /* 2. 段表,之后才敢装中断 */
    idt_init();              /* 3. 中断门 */
    pic_remap();             /* 4. 重映射 8259,让 IRQ 不再撞异常 */
    pit_init(100);           /* 5. 100Hz 时钟,调度的心跳 */

    servers_start();         /* 6. 创建 fs / tty / shell 三个任务 */
    intr_enable();           /* 7. 开中断,世界开始转动 */

    for (;;) __asm__ volatile("hlt");   /* 内核本体空闲 */
}
```

注意最后这个死循环:内核 `kmain` 建完舞台就**主动让出 CPU**。此后所有工作都发生在
`servers_start` 创建的任务里。这正是微内核的姿态——内核不是主角,只是舞台。

## 一个顺序上的坑

必须先 `idt_init` 再 `intr_enable`。如果在中断门还没装好时就开中断,第一个时钟滴答
会跳到一个空向量,机器当场三重故障复位。初学内核最常见的“一开机就重启”多半是这个。

## 本章文件

```
boot/boot.S     Multiboot 头 + _start
boot/main.c     kmain:初始化硬件,拉起服务,开中断
boot/kernel.ld  链接脚本,魔数置于文件首
```

下一章:[中断分发](/guide/interrupts)。
