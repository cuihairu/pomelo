# 13 · 分页上线

对应代码:`kernel/mm/`

## 第一个只有内核能做的事

[第 1 章](/guide/intro)说过,划分内核与服务的标准是"每个任务都绕不开的"。
内存翻译正是这样的东西:一台机器同时想用 0x40000000 这块地址干几件不同的事,
只有 CPU 里的 MMU 能裁决。于是 `kernel/` 里多了第五个机制——分页,前四个机制
一章一个的故事,这次是两个:本章管"地址怎么翻",下一章管"世界怎么换"。

## 帧:一张位图

分页之前得先有页可分。物理内存按 4 KB 切成帧,分配器就是一张位图,`frame.c`
的全部家当:

```c
static u8 used[NFRAMES / 8];
static u32 first_free;

extern u8 kernel_end[];                  /* boot/kernel.ld */

void frame_init(void) {
    first_free = (((u32)kernel_end + 4095) & ~4095u) / 4096;
    for (u32 f = 0; f < first_free; f++) mark(f);
}
```

`kernel_end` 是链接脚本里的一个符号:内核镜像到哪结束,哪以下的帧(BIOS 数据、
VGA 文本、内核自己的 .bss)就整体标记为已用。位图从头扫到尾,第一个空位就是
分配结果——16 MB 内存、几十个用户帧的规模下,这比链表省事,比教科书诚实。

这张位图不用隔着书页想象——shell 里的 `mem` 命令就是它的一面镜子:

```
pomelo> mem
frames: 3791 of 4096 free
```

`SYS_MEM` 把位图数一遍,关着中断数,免得数到一半时钟把一帧发出去、三个数
对不上账。自己动手就能看见内核吃内存:敲一次 `mem`,再 `spawn probe`,再
`mem`——少的那些帧,就是 probe 的新地址空间(代码页 + 页表)。分配器从
"书上的一段代码"变成了"机器上一条看得见的账目"。

## 恒等映射:地址一个都不变

内核自己的页目录做的是最保守的事:把低 16 MB **原样映射**到原地址。

```c
void paging_init(void) {
    frame_init();
    for (u32 i = 0; i < NPT; i++) {
        kernel_pdir[i] = (u32)lo_pt[i] | PTE_P | PTE_RW;
        for (u32 j = 0; j < 1024; j++)
            lo_pt[i][j] = ((i << 22) | (j << 12)) | PTE_P | PTE_RW;
    }
    load_cr3((u32)kernel_pdir);
    __asm__ volatile("movl %%cr0, %%eax\n"
                     "orl  $0x80000000, %%eax\n"   /* CR0.PG */
                     "movl %%eax, %%cr0" ::: "eax", "cc");
}
```

翻开 CR0 的 PG 位那一刻,MMU 开始工作,而内核毫无察觉——每条已经写好的取指、
每次 `outb`、每个栈操作,翻译前后是同一个地址。这是刻意的:`PTE_RW` 但不带
`PTE_US`,低 16 MB 是**内核专属**;ring 3 一进来,这半边世界就对它关了门。
机制先立起来,地址不变,才轮到下一章动地址。

这张目录还有个身份:所有用户页目录的**内核半边**。下一章会看到,新建用户
目录就是把这四条 PDE 原样拷过去——内核在任何任务的地址空间里都长一个样。

## 页错误来了怎么办

分页一开,CPU 就多了一类事要报告:14 号异常,页错误。`intr.c` 的分发现在
长这样:

```c
if (r->int_no == 14) {                /* page fault */
    task_fault(r, "page fault", cr2_fault());
    return;
}
```

`cr2_fault()` 读 CR2——CPU 在这里记下了出错的地址,这是 MMU 递来的唯一物证。
怎么处置,取决于出事的人是谁:内核自己页错误是内核的 bug,panic 停机;ring 3
任务页错误,杀掉的是那个任务。判决逻辑在下一章。

## 本章文件

```
kernel/mm/frame.c/.h  物理帧位图分配器
kernel/mm/paging.c/.h 内核页目录、恒等映射、cr3 与 CR0
```

下一章:[真隔离:ring3](/guide/ring3)。
