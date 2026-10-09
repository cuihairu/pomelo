# Pomelo

> A microkernel written from scratch in C — small enough to read in one sitting.

Pomelo is a teaching kernel for the i386, built as a **logical microkernel**: the
kernel only does five things (interrupt dispatch, scheduling, IPC, a syscall
gate, paging). Everything else — the file system, the terminal, the shell — runs
as separate tasks on top of message passing, and the shell lives in its own
ring 3 address space. It boots straight from QEMU with no bootloader to install.

[![nightly](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml/badge.svg)](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml)

[中文说明](README.zh.md)

## Quick start

```sh
cmake -B build && cmake --build build
ctest --test-dir build          # boots in QEMU, verifies the banner
```

## Run it locally

QEMU is the only runtime dependency:

| OS | install |
| --- | --- |
| Ubuntu / Debian | `sudo apt install qemu-system-x86 qemu-utils` |
| Fedora / CentOS | `sudo dnf install qemu-system-x86` |
| macOS | `brew install qemu` |
| Windows | `winget install qemu` |

Confirm with `qemu-system-i386 --version`, then pick a mode:

```sh
# serial mode: the shell lands in your terminal (quit: Ctrl+A, then X)
qemu-system-i386 -M pc -kernel build/kernel -hda build/pomelo.img -nographic

# display mode: a real window, keyboard input via the emulated PS/2
qemu-system-i386 -M pc -kernel build/kernel -hda build/pomelo.img
```

`-M pc` is not decoration: the default `q35` machine wires its disk through
AHCI, out of reach of this ATA driver — the fs server prints a note and
parks. The [nightly workflow](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml)
boots both machines every day and ships the serial log and a boot
screenshot with its [release](https://github.com/cuihairu/pomelo/releases/tag/nightly).

## Layout

Each directory is one concept, and the book follows the same order. The split
is the point: `kernel/` holds only the five mechanisms every task must pass
through, `servers/` holds the user-space services, and apps reach them purely
by passing messages. The kernel never parses a disk format and never touches
a keyboard; the file system lives outside it so that a disk bug has to get
through IPC before it can hurt anyone.

```
boot/       From power-on to the first line of C: multiboot, GDT, IDT, PIT.
kernel/     The microkernel: interrupts, scheduling, IPC, syscalls, paging.
servers/fs/ A file server and its tiny on-disk format (superblock + inodes).
servers/tty/ A terminal server: keyboard scancodes to a line buffer.
apps/shell/ A REPL that talks to the services over IPC (runs at ring 3).
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
