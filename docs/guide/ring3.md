# 14 · 真隔离:ring3

对应代码:`boot/gdt.c`、`kernel/sched/`、`kernel/mm/paging.c`、`apps/probe/`

## shell 搬进了自己的地址空间

[上一章](/guide/paging)把分页机制立了起来:低 16 MB 恒等映射,内核专属。本章
把 shell 搬出这半边世界——它从此跑在 ring 3、自己的页目录里,和内核之间隔着
一堵 CPU 亲自把守的墙。shell 的代码一行没改:它本来就只发消息。这是微内核
结构投资的回报,当初守规矩,如今升级免费。

## 用户程序是一段链接在 0x40000000 的二进制

用户程序链接在 `USER_BASE = 0x40000000`,那是它自己的世界;产物流水线三步:

```sh
ld -T tools/user.ld -o ushell  ...   # 链接成用户 ELF
objcopy -O binary ushell ushell.bin  # 削成平铺二进制
ld -r -b binary -o ushell_blob.o ushell.bin   # 塞回内核当数据
```

两个设计都在 `tools/user.ld` 里。第一,**入口写在文件头**:`.hdr` 段骑在最前面,
内容是一个常量——入口地址,内核读 blob 的前四个字节就知道往哪跳,不用解析 ELF。
第二,**.bss 折进 .data**:平铺二进制不携带 NOBITS 段,把未初始化变量并进
有内容的段,文件里的字节就是任务将摸到的全部,内核拷完即用,没有"再补零"
的第二步。

## GDT 的另一半

`boot/gdt.c` 里现在有六个描述符,新增的三样各司其职:

```c
{0xFFFF, 0, 0, 0xFA, 0xCF, 0},   /* 0x18: code, ring 3 */
{0xFFFF, 0, 0, 0xF2, 0xCF, 0},   /* 0x20: data, ring 3 */
{0x0067, 0, 0, 0x89, 0x40, 0},   /* 0x28: TSS, filled in below */
```

ring 3 段和内核段一样覆盖 4G——**墙不是段砌的,是页砌的**;段只负责把特权级
从 0 降到 3。TSS 只有一个字段有用:`esp0`,ring 3 敲门(int 0x80 或中断)时
CPU 自动切过来的内核栈。每任务一个内核栈,TSS 里只放当前任务的那一个,
切换时重写。

## 出生即 ring 3

`task_spawn_user`(kernel/sched/sched.c)给新任务铺的不是普通栈,而是一副
完整的 iret 帧:

```c
*--sp = USER_DS;              /* ss: the frame iret walks bottom-up */
*--sp = ustack_top;           /* user esp */
*--sp = 0x202;                /* eflags: interrupts on */
*--sp = USER_CS;              /* user cs */
*--sp = entry;                /* user eip */
*--sp = (u32)ring3_entry;     /* switch_to's ret target */
```

`switch_to` 照旧弹寄存器、`ret` 进 `ring3_entry`(switch.S),它只做两件事:
把 ds/es 换成用户数据段,然后 `iret`——CPU 看到栈上的 RPL=3,连 eip 带特权级
一起换,落进用户代码。此后它回内核只有一条路:0x80 门。

## 换任务就是换世界

调度器多切两样东西(kernel/sched/sched.c):

```c
static void sched_switch(int next) {
    int prev = cur;
    cur = next;
    tss_esp0_set((u32)(kstacks[next] + STACK_BYTES));
    load_cr3(tasks[next].pdir ? tasks[next].pdir : (u32)kernel_pdir);
    switch_to(&tasks[prev].esp, tasks[next].esp);
}
```

cr3 一换,页表就换,用户区域里 0x40000000 指向谁也跟着换——两个用户任务
共用同一个虚拟地址而互相看不见,这正是隔离的本意。页目录怎么来?`pdir_user_new`
(kernel/mm/paging.c)领一个目录帧、一张用户页表,把内核的四条 PDE 原样拷贝,
再把镜像逐帧拷进用户帧。借的还是恒等映射的红利:所有帧都在低 16 MB、内核
可见,建表和拷贝全程不用中途切 cr3。

## 开机自证:probe

光说墙结实不算数。`apps/probe/` 是一段专程来撞墙的用户程序:它先规规矩矩
通过 tty 服务打印一行,然后伸手摸内核的 .bss:

```c
volatile u32 *kernel = (volatile u32 *)0x101000;   /* kernel .bss */
u32 s = *kernel;                                   /* #PF right here */
```

那个地址在它的页目录里是**内核专属**页,CPL=3 碰它,现场就是页错误。每次
开机的真实日志:

```
pomelo booting (multiboot magic=2badb002)
paging on: low 16m identity
task 4 killed: page fault at 101000 (eip=40000055)

pomelo shell - type 'help'
```

看 `eip=40000055`:出错代码住在用户区,惹祸的地址在内核区,墙正好砌在中间。
fs、tty、shell 照常运行,smoke 和 nightly 都把这两行列为必查项——隔离从
"纪律"变成"测试断言"。

## 门口的规矩

ring 3 进内核的门只有一扇:`int 0x80`,门上 `DPL=3`(flags 0xEE)——用户
可以敲,硬件中断门(DPL=0)它敲了就是 general protection fault。而 13/14
两个异常如今各有一条专属通路,判决在 `intr.c`:

```c
static void task_fault(struct regs *r, const char *what, u32 addr) {
    if ((r->cs & 3) == 0)
        panic(what, r);            /* 内核自己出错,停机 */
    intr_disable();
    kprintf("task %d killed: %s at %x (eip=%x)\n", cur, what, addr, r->eip);
    tasks[cur].state = ST_FREE;
    sched_next();
}
```

内核页错误 = 内核 bug = 停机;用户页错误 = 一个任务作死 = 杀一个,其他人
无恙。`sched_next` 转身就调度别的任务,死任务的栈永远不会被续上。

## 还差的一步

诚实地说,隔离只兑现了一半:shell 和 probe 住进了各自的地址空间,但 fs、tty
还是 ring 0 的内核任务,和内核共享低 16 MB;IPC 的消息也是内核替两边直接
读写队列,还没有跨地址空间拷贝。补齐这两件事——**每服务独立页表 + IPC 内核
拷贝**——就是把 [第 1 章](/guide/intro)的升级清单全数清账,留在路线图上。

## 本章文件

```
boot/gdt.c/.h           ring 3 段、TSS、esp0
tools/user.ld           用户链接脚本(.hdr 入口 + .bss 折叠)
kernel/sched/sched.c    task_spawn_user 的 iret 帧、cr3/esp0 切换
kernel/sched/switch.S   ring3_entry
kernel/mm/paging.c      pdir_user_new:每任务一份世界
apps/probe/probe.c      开机撞墙演示
```

本章之后没有新章节了。想把[第 1 章](/guide/intro)升级清单上的最后两项
(每服务独立页表、IPC 内核拷贝)做掉,从[构建与运行](/guide/build-and-run)
把机器跑起来改起。
