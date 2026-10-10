# 10 · shell

对应代码:`apps/shell/`

## 最小的 REPL

shell 是全书代码最少的一章,却是前面所有机制的**总装车间**:

```c
void shell_main(void) {
    static char line[64];
    tty_puts("\npomelo shell - type 'help'\n");
    for (;;) {
        tty_puts("pomelo> ");
        tty_getline(line, sizeof line);       /* 向 tty 服务要一行 */
        run(line);                            /* 查表分发 */
    }
}
```

读一遍这三行,你会发现 shell 没有 `main` 之外的循环、没有中断、没有驱动——
它只是不停地“要一行、跑一条”。

## 命令分发表

命令的实现散在多个小文件里,靠一张表收拢:

```c
struct cmd { const char *name; void (*fn)(int argc, char **argv); };

static const struct cmd cmds[] = {
    { "help",   cmd_help   },
    { "ls",     cmd_ls     },
    { "cat",    cmd_cat    },
    { "write",  cmd_write  },
    { "echo",   cmd_echo   },
    { "clear",  cmd_clear  },
    { "ps",     cmd_ps     },
    { "spawn",  cmd_spawn  },
    { "uptime", cmd_uptime },
    { "reboot", cmd_reboot },
    { 0, 0 }
};
```

`run` 把输入按空格切成 `argv`,线性查表,命中就调用。加一条新命令 = 写一个
`cmd_xxx` 函数 + 表里加一行,**不用碰 shell 的任何既有代码**。这是把“数据驱动”
用在最朴素的地方。

`echo` 就是这条规则的最小样板(`cmd_echo.c`):不碰 fs、不碰内核,把 `argv`
借 tty 说出去就完事——想加自己的命令,照抄它起步。其余命令各按依赖落座:
`ls`/`cat`/`write` 走 fs,`ps`/`spawn`/`uptime` 走系统调用门(第 15 章、第 4 章),
`clear`/`reboot` 各归其主。看一条命令依赖谁,就知道它的消息会经过谁。

## 两条文件命令走完 IPC 全程

`cat` 是检验前几章的集成测试:

```c
void cmd_cat(int argc, char **argv) {
    if (argc < 2) { tty_puts("usage: cat <file>\n"); return; }

    int size;
    int ino = fs_open(argv[1], &size);        /* 问 fs:有这文件吗 */
    if (ino < 0) { tty_puts("no such file\n"); return; }

    char buf[MSG_DATA];
    for (int off = 0; off < size; ) {
        int n = fs_read(ino, off, buf);       /* 分批取数据 */
        if (n <= 0) break;
        tty_write(buf, n);
        off += n;
    }
}
```

`write` 反向走同一条路(`FS_WRITE`),把参数拼成文件内容发过去。
`ls` 则是一个 `FS_LS` 消息拿到整个目录。看清楚:shell 与文件系统之间**没有一行
共享代码**,只有消息往返——第 5 章时序图在这里落地成了真代码。

## 一条命令的完整旅程(全书总结)

以 `cat hello.txt` 为例,从指尖到磁盘:

```
敲键        → IRQ1 → kernel/intr 桩 → irq_raise(1)
            → tty 任务被 irq_wait 唤醒 → 攒行 → 回 TTY_GETLINE
shell 醒来  → run("cat hello.txt") → cmd_cat
            → SYS_SEND(FS_OPEN) → int 0x80 → syscall 门 → ipc_send
fs 醒来     → 查 inode 表 → ata_read(超级块、inode、数据块) → 回消息
shell 醒来  → SYS_SEND(TTY_PUTS) → tty 回显 → 你看到文件内容
```

每一次穿越,都是你已经读过的那几段代码。操作系统没有魔法,只有分工。

## 本章文件

```
apps/shell/shell.h      命令表结构与全部包装的声明
apps/shell/shell.c      REPL 主循环与分发表
apps/shell/cmd_help.c   help
apps/shell/cmd_fs.c     ls / cat / write
apps/shell/cmd_echo.c   echo:最小命令样板
apps/shell/cmd_sys.c    clear / uptime / reboot
apps/shell/cmd_proc.c   ps / spawn(第 15 章)
apps/shell/fsio.c       fs 协议客户端包装(含 MSG_FS_ERR 应答)
```

下一章:[构建与运行](/guide/build-and-run)。
