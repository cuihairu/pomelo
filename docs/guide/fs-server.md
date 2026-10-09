# 08 · 文件服务

对应代码:`servers/fs/`(除 format.h)

## 文件系统变成了一个“收发消息的程序”

上一章的格式只是磁盘上的字节排布,真正让它活起来的是 fs 服务——一个普通的任务,
循环做一件事:收请求,查 inode,读写块,回消息。

```c
void fs_main(void) {
    if (ata_read(0, &sb) || sb.magic != SB_MAGIC) {
        kprintf("fs: bad disk\n");
        return;                       /* 没盘就躺平,别拖垮系统 */
    }
    struct msg m;
    for (;;) {
        sys_recv(&m);
        switch (m.type) {
        case FS_LS:    do_ls(&m);      break;
        case FS_OPEN:  do_open(&m);    break;
        case FS_READ:  do_read(&m);    break;
        case FS_WRITE: do_write(&m);   break;
        }
    }
}
```

一眼望去像个网络服务器——事实上它就是。微内核里的“系统服务”和分布式系统里的
“服务”在结构上没有区别,这正是微内核教学的价值。

## ATA:跟 1980 年代的硬盘说话

读一个扇区(`servers/fs/ata.c`)是纯粹的时序等待:

```c
int ata_read(u32 lba, void *buf) {
    ata_wait();                    /* 等盘不再忙 */
    outb(0x1F2, 1);                /* 读 1 扇区 */
    outb(0x1F3, lba);              /* LBA 拆成 4 段写进端口 */
    outb(0x1F4, lba >> 8);
    outb(0x1F5, lba >> 16);
    outb(0x1F6, 0xE0 | (lba >> 24) & 0xF);
    outb(0x1F7, 0x20);             /* 0x20 = READ SECTORS 命令 */
    ata_wait();
    insw(0x1F0, buf, 256);         /* 一次搬 256 个 16 位字 = 512 字节 */
    return 0;
}
```

没有 DMA、没有中断、没有缓存——**轮询**。每一步都是查状态端口。这三十行是全书
唯一跟真实硬件时序贴身肉搏的地方,也正因为它被关在 fs 服务里,内核对此一无所知。

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

## 写文件

`FS_WRITE` 稍多一步:若 inode 未用,先找空闲 inode,再从数据区**顺序**领块
(第 7 章的约定),把数据写进去,最后补写 inode 和超级块。注意顺序:**先写数据,
再写 inode**——万一中途断电,最坏是留下孤儿数据块,而不是一个指向垃圾的“假文件”。

## 本章文件

```
servers/fs/ata.c     ATA PIO 轮询读写扇区
servers/fs/fs.c      服务循环 + ls/open/read/write 的实现
servers/fs/fs.h      消息类型常量(FS_LS 等)
```

下一章:[终端服务](/guide/tty-server)。
