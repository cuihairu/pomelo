# Pomelo

> A microkernel written from scratch in C — small enough to read in one sitting.

Pomelo is a teaching kernel for the i386, built as a **logical microkernel**: the
kernel only does four things (interrupt dispatch, scheduling, IPC, a syscall
gate). Everything else — the file system, the terminal, the shell — runs as
separate tasks on top of message passing. It boots straight from QEMU with no
bootloader to install.

[中文说明](README.zh.md)

## Quick start

```sh
cmake -B build && cmake --build build
ctest --test-dir build          # boots in QEMU, verifies the banner
```

## Layout

Each directory is one concept, and the book follows the same order.

```
boot/       From power-on to the first line of C: multiboot, GDT, IDT, PIT.
kernel/     The microkernel: interrupts, scheduling, IPC, syscalls. Nothing else.
servers/fs/ A file server and its tiny on-disk format (superblock + inodes).
servers/tty/ A terminal server: keyboard scancodes to a line buffer.
apps/shell/ A REPL that talks to the services over IPC.
tools/mkfs/ The host tool that lays out the disk image.
docs/       The book (VitePress), one chapter per directory above.
```

## Documentation

The full text lives in `docs/` and is built with VitePress:

```sh
npm --prefix docs install
npm --prefix docs run dev
```

## License

MIT
