# 08 · The file service

Corresponding code: `servers/fs/` (except format.h)

## The filesystem becomes a program that receives and sends messages

The format of the previous chapter is just a byte layout on disk; what makes it really live is the fs service — an ordinary ring 3 task (a blob embedded in the kernel image, just like shell, living in its own address space), doing one thing in a loop: receive a request, look up the inode, read and write blocks, send a message back.

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

"Cannot serve" and "cannot answer" are two different things. When fs finds the disk unusable it records its state as `why`, and from then on answers every request with an `MSG_FS_ERR`, the reason carried in the data — the shell on the other side prints `fs: no disk`, and the prompt comes back as usual. The same applies when the inode table is full, with `fs: no space`. The protocol deliberately keeps one message type for this: a service may refuse a request, but it may not play dead. That is a good cut better than the "docked and not answering" writing of the old days: the fault is still fenced inside one service (the kernel and the other services live on as usual, and that insurance did not shrink one bit), but what the client gets is not indefinite silence but an answer it can take to error reporting. A refusal and "answered properly with a no" are hereby two separate things: no such file found, no such name — those go through the normal answer and the client phrases it itself; only when the service does not show up for work does it come to a refusal. Sending failure back as an answer is the one line most easily forgotten in IPC protocol design. (`con_puts` comes from `apps/con.h`: a program in ring 3 that wants to say something has only the `sys_write` path.)

At a glance it looks like a network server — and in fact it is one. The "system service" of a microkernel and the "service" of a distributed system are structurally indistinguishable, and that is precisely why a microkernel is easy to read.

## ATA: talking to a 1980s-era disk

Reading one sector (`servers/fs/ata.c`) is three steps: issue the command, wait for the data to be ready, move the data:

```c
int ata_read(u32 lba, void *buf) {
    if (ata_start(lba, 0x20) < 0) return -1;  /* READ SECTORS */
    if (ata_drq() < 0) return -1;             /* wait for data to appear on the data port */
    insw(ATA_DATA, buf, SECTOR / 2);          /* 256 16-bit words = 512 bytes */
    return 0;
}
```

No DMA, no interrupts, no cache — **polling**. But there are two ways to "wait." The brute force is to spin in place checking the status port; in ata.c every wait has a `sys_yield()` inserted inside the loop: give the CPU to the next task, and check again when it comes back around. Polling without courtesy pins the CPU down for the whole transfer — and in the simulator, where you can just barely see the host thread finishing its command, the machine really does hang. (The classic trick is a `hlt` nap, but `hlt` is a ring 0-only instruction; once you are in ring 3, the polite move is to hand the scheduler back.)

This chapter also carries one real piece of battle damage: the ATA ports (0x1F0..) hang off the PIIX3 of QEMU's `pc` machine type, while the `q35` machine type puts the disk behind AHCI, and those ports are **dangling** — what comes back off a dangling bus is 0xFF, whose BSY bit happens to be 1, and a naive wait loop will wait forever for a disk that is not there. So every wait goes through `ata_dead()` first: if the status byte is 0xFF, it errors and returns immediately, giving fs a chance to send "no" back as the answer. The driver not trusting the bus is the same upbringing as an IPC message checking its return value.

There is one more door crack that belongs to this chapter alone: the driver now runs in ring 3, where port instructions like `in` and `out` are forbidden by default at this privilege level. The key x86 hands over is called **IOPL**: two bits in eflags, and when `IOPL >= CPL` port instructions are allowed. The kernel sets `IOPL=3` in the birth eflags of fs alone (`EFLAGS_USER_IOPL`, kernel/sched/sched.h) — the disk driver and the file service living together is a design we have kept since Chapter 1; IOPL is the cat flap cut into that wall especially for it: not one extra instruction can be handed to anyone, and other tasks still cannot touch the ports.

## The life of one READ request

```
1. shell   SEND{FS_OPEN, name="hello.txt"} ──▶ fs receives
2. fs      scan the inode table, used && name matches → inode number i, reply with size
3. shell   SEND{FS_READ, arg0=i, arg1=off, len} ──▶ fs
4. fs      direct[k] = (off / 512), work out the LBA
5. fs      ata_read(lba, buf), cut out the requested slice from buf
6. fs      reply message, the data goes into msg.data
```

Step 4 is the only "arithmetic" of the filesystem: converting a byte offset into a block number. After these six steps you understand what the essence of the `pread` syscall is doing.

## Writing a file: a three-part protocol

One message cannot pull it off, so fs splits it into three parts, sent by shell in order:

```
1. shell  SEND{FS_CREATE, data=name} ──▶ fs claims a free inode, replies with the inode number
2. shell  SEND{FS_WRITE, arg0=i, arg1=off, data=...} ──▶ fs writes the data block
3. shell  SEND{FS_COMMIT, arg0=i, arg1=size} ──▶ fs pins down the inode and the superblock
```

This is a **two-phase commit**: the data hits the disk first, the size lands last. If the power dies halfway through, the worst case is orphaned data blocks left behind (all-zero blocks get re-claimed, see `alloc_block`), not a "fake file" pointing at garbage. Block allocation has no bitmap and no free list — **an all-zero block is a free block**, a block that has been written is no longer all-zero, so one is never allocated twice.

One more plain-on-the-sentence semantic of create: **it does not check for duplicate names**. It claims the first free inode and writes the name into it, even if a file with the same name is already on disk — `ls` lists every used inode, so the same name produces two rows, and `cat` goes for whichever one it scans first. There is no such thing as deleting, so the walk.txt written every night by the walk script piles up higher and higher on a long-lived disk; to get the lab disk back to clean, just lay a fresh one.

## Files of this chapter

```
servers/fs/ata.c     ATA PIO polling reads and writes sectors
servers/fs/fs.c      the service loop + ls/open + replies and refusals
servers/fs/fsrw.c    read/create/write/commit: the implementation of the three-part write protocol
servers/fs/fs.h      message type constants (FS_LS and so on)
apps/con.h          the talking wrapper for ring 3: con_puts only goes through sys_write
```

The next chapter: [the terminal service](/guide/tty-server).
