> [Chinese](shell.md) | **English**

# 10 · shell

Corresponding code: `apps/shell/`

## The smallest REPL

The shell itself carries almost no new mechanism, yet it is the **assembly plant** of every mechanism before it:

```c
void shell_main(void) {
    static char line[64];
    tty_puts("\npomelo shell - type 'help'\n");
    for (;;) {
        tty_puts("pomelo> ");
        tty_getline(line, sizeof line);       /* ask the tty service for a line */
        run(line);                            /* look it up and dispatch */
    }
}
```

Read these three lines once and you find the shell has no loop besides `main`, no interrupts, no drivers — it just goes on "asking for a line, running one".

## The command dispatch table

The implementations of the commands are scattered across several small files, and one table pulls them together:

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
    { "mem",    cmd_mem    },
    { "reboot", cmd_reboot },
    { 0, 0 }
};
```

`run` splits the input on spaces into `argv`, walks the table linearly, and calls on a hit. Adding a new command = write a `cmd_xxx` function + one more row in the table, **without touching any existing code in the shell**. That is "data-driven" applied in its plainest place.

`echo` is the smallest model of this rule (`cmd_echo.c`): it touches neither fs nor the kernel, it just lends tty to say the `argv` out and that is the end of it — to add your own command, copy it to get started. The other commands each sit by their dependency: `ls`/`cat`/`write` go through fs, `ps`/`spawn`/`uptime`/`mem` go through the syscall gate (Chapter 15, Chapter 4), `clear`/`reboot` each go to their own master. See who a command depends on and you know who its messages will pass through.

## Verified: clear and reboot

Of the ten commands in the table, the first eight leave an answer on the serial port, and `smoke.sh` and `walkthrough.sh` score them one by one; the last two say nothing, so each gets its own dedicated test — precisely because they do not speak, something else has to serve as evidence. `clear`'s answer is the absence of an answer: it sends one message to tty, and tty, replying one `SYS_CLEAR`, wipes `0xb8000`. So `smoke-clear.sh` types `mem` with `sendkey` on a machine whose serial port is attached to null, takes a VGA screenshot, then types `clear` and takes a second one — what is judged is that **the second screenshot shows nothing but the prompt**. `reboot` has no answer at all to speak of: the machine is supposed to stop along with it. QEMU is started with `-no-reboot`, so a real stop is status 0, and sitting until `timeout` kills it is 124 — what `smoke-reboot.sh` watches is exactly this exit status; the `rebooting...` on the serial port only proves that the prompt line got printed.

## Two file commands walk the whole IPC journey

`cat` is the integration test for the chapters before:

```c
void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        tty_puts("usage: cat <file>\n");
        return;
    }
    int size;
    int ino = fs_open(argv[1], &size);        /* ask fs: is there such a file */
    if (ino == FS_REFUSED) return;         /* the reason is already printed */
    if (ino < 0) {
        tty_puts("no such file\n");
        return;
    }
    char buf[MSG_DATA];
    for (int off = 0; off < size; ) {
        int n = fs_read(ino, off, buf);       /* take the data back in batches */
        if (n <= 0) break;
        tty_write(buf, n);
        off += n;
    }
}
```

`write` walks the same road in reverse (`FS_WRITE`), splicing the arguments into file contents and sending them over. `ls` gets the whole directory with a single `FS_LS` message. Look clearly: between shell and the filesystem there is **not one line of shared code**, only messages going back and forth — the sequence diagram of Chapter 5 lands here as real code.

## A command's complete journey (summary of the whole book)

Take `cat hello.txt`, from fingertip to disk:

```
keystroke    → IRQ1 → kernel/intr stub → kbd_isr(kernel/char.c)
             → TTY_CHAR message wakes tty → accumulate the line → reply TTY_GETLINE
shell wakes  → run("cat hello.txt") → cmd_cat
             → SYS_SEND(FS_OPEN) → int 0x80 → syscall gate → ipc_send
fs wakes     → look up the inode table → ata_read(superblock, inode, data block) → reply message
shell wakes  → SYS_SEND(TTY_PUTS) → tty echoes back → you see the file contents
```

Every crossing is a piece of code you have already read. There is no magic in an operating system, only division of labor.

## Files of this chapter

```
apps/shell/shell.h      the command table struct and the declarations of all wrappers
apps/shell/shell.c      the REPL main loop and the dispatch table
apps/shell/cmd_help.c  help
apps/shell/cmd_fs.c    ls / cat / write
apps/shell/cmd_echo.c  echo: the smallest command model
apps/shell/cmd_sys.c   clear / uptime / mem / reboot
apps/shell/cmd_proc.c  ps / spawn (Chapter 15)
apps/shell/fsio.c      the fs protocol client wrapper (including the MSG_FS_ERR reply)
apps/con.h             the ring 3 screen-write wrappers: con_write / con_clear / con_puts
```

The next chapter: [Building and running](/guide/build-and-run.en).
