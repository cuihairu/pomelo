# 08 · 文件服务

对应代码:`servers/fs/`(除 format.h)

## 文件系统变成了一个“收发消息的程序”

上一章的格式只是磁盘上的字节排布,真正让它活起来的是 fs 服务——一个普通的
ring 3 任务(和 shell 一样是嵌进内核镜像的 blob,住自己的地址空间),循环做
一件事:收请求,查 inode,读写块,回消息。

```c
void fs_main(void) {
    struct msg m;
    const char *why = 0;          /* why the disk is unusable, if it is */
    if (ata_read(0, &sb) < 0) {
        con_puts("fs: no disk behind the ata ports, answering no\n");
        why = "no disk";
    } else if (sb.magic != FS_MAGIC) {
        con_puts("fs: bad disk magic, answering no\n");
        why = "bad magic";
    } else {
        for (int s = 0; s < INODE_SECTORS; s++)
            ata_read(INODE_START + s, (u8 *)inodes + s * SECTOR);
    }

    for (;;) {
        sys_recv(&m);
        if (why) { refuse(&m, why); continue; }   /* cannot serve: say so */
        switch (m.type) {
        case MSG_FS_LS:     do_ls(&m);     break;
        case MSG_FS_OPEN:   do_open(&m);   break;
        case MSG_FS_READ:   do_read(&m);   break;
        case MSG_FS_CREATE: do_create(&m); break;
        case MSG_FS_WRITE:  do_write(&m);  break;
        case MSG_FS_COMMIT: do_commit(&m); break;
        }
    }
}
```

“不能服务”和“不能答话”是两回事。fs 发现盘不可用时把自己的状态记成 `why`,
从此对每个请求回一条 `MSG_FS_ERR`,data 里带上原因——shell 那头打印出
`fs: no disk`,提示符照常回来。协议里专门留了这一种消息:服务可以拒绝请求,
但不可以装死。这比早年间“停靠不回话”的写法好一截:故障照样被关在一个服务里
(内核和其余服务照常活着,这份保险一点没少),而客户端拿到的不是无期的沉默,
是一句可以拿去报错的答复。把失败作为答案送回来,是 IPC 协议设计里最容易被
忘掉的一条。(`con_puts` 来自 `apps/con.h`:ring 3 的程序想说话,只有
`sys_write` 这一条路。)

一眼望去像个网络服务器——事实上它就是。微内核里的“系统服务”和分布式系统里的
“服务”在结构上没有区别,这正是微内核教学的价值。

## ATA:跟 1980 年代的硬盘说话

读一个扇区(`servers/fs/ata.c`)是三步:发命令,等数据就绪,搬数据:

```c
int ata_read(u32 lba, void *buf) {
    if (ata_start(lba, 0x20) < 0) return -1;  /* READ SECTORS */
    if (ata_drq() < 0) return -1;             /* 等数据挂上数据端口 */
    insw(ATA_DATA, buf, SECTOR / 2);          /* 256 个 16 位字 = 512 字节 */
    return 0;
}
```

没有 DMA、没有中断、没有缓存——**轮询**。但“等”有两种等法。蛮力是原地转圈查
状态端口;ata.c 里每次等待都在循环里插了 `sys_yield()`:把 CPU 让给下一个任务,
轮到自己再查。不谦让的轮询会把 CPU 钉死整个传输期——在模拟器里,这恰好看不见
宿主线程把命令做完,系统就真的卡住了。(经典写法是 `hlt` 小睡,但 hlt 是
ring 0 专属指令;到了 ring 3,礼貌的做法就是交还调度器。)

这一章还有个真实的战损:ATA 端口(0x1F0..)挂在 QEMU `pc` 机型的 PIIX3 上,
而 `q35` 机型把盘接在 AHCI 后面,这些端口**悬空**——从悬空总线读回来的是
0xFF,它的 BSY 位恰好是 1,天真的等待循环会永远等一台不存在的盘。所以每次
等待先过一遍 `ata_dead()`:状态字节是 0xFF 就立刻报错返回,让 fs 有机会
把"不"作为答案送回去。驱动不信任总线,和 IPC 消息要检查返回值,是同一种教养。

还有一道只属于本章的门缝:驱动如今跑在 ring 3,`in`/`out` 这类端口指令在
这个特权级默认是禁令。x86 给的钥匙叫 **IOPL**:eflags 里的两位,`IOPL >= CPL`
时放行端口指令。内核只给 fs 这一家的出生 eflags 置了 `IOPL=3`(`EFLAGS_USER_IOPL`,
kernel/sched/sched.h)——磁盘驱动和文件服务住在一起,是我们从第 1 章守到现在的
设计;IOPL 就是这堵墙上专门为它开的猫洞:一条指令都多给不了,别的任务照样
摸不到端口。

## 一条 READ 请求的生命

```
1. shell   SEND{FS_OPEN, name="hello.txt"} ──▶ fs 收到
2. fs      扫 inode 表,used && name 相同 → inode 号 i,回 size
3. shell   SEND{FS_READ, arg0=i, arg1=off, len} ──▶ fs
4. fs      direct[k] = (off / 512),算出 LBA
5. fs      ata_read(lba, buf),从 buf 里切出请求的那一段
6. fs      回消息,数据放进 msg.data
```

第 4 步是文件系统唯一的“算术”:字节偏移换算成块号。学完这六步,你就理解了
`pread` 系统调用在干什么的本质。

## 写文件:三段协议

写一条消息搞不定,fs 把它拆成三段,由 shell 依次发:

```
1. shell  SEND{FS_CREATE, data=name} ──▶ fs 领一个空 inode,回 inode 号
2. shell  SEND{FS_WRITE, arg0=i, arg1=off, data=...} ──▶ fs 写数据块
3. shell  SEND{FS_COMMIT, arg0=i, arg1=size} ──▶ fs 落定 inode 和超级块
```

这是**两段提交**:数据先落盘,size 最后落。万一中途断电,最坏是留下孤儿数据块
(全零块会被重新认领,见 `alloc_block`),而不是一个指向垃圾的“假文件”。
块分配没有位图、没有空闲链表——**全零块就是空闲块**,写过的块不再全零,
所以永远不会被分配两次。

## 本章文件

```
servers/fs/ata.c     ATA PIO 轮询读写扇区
servers/fs/fs.c      服务循环 + ls/open/read/write 的实现
servers/fs/fs.h      消息类型常量(FS_LS 等)
```

下一章:[终端服务](/guide/tty-server)。
