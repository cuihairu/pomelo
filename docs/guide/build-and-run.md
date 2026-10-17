# 11 · Building and running

Corresponding code: `CMakeLists.txt`, `tools/mkfs/mkfs.c`, `tools/smoke.sh`

## Installing QEMU

QEMU plays the i386 machine and is the only runtime dependency. How to install it:

| Platform | Command |
| --- | --- |
| Ubuntu / Debian | `sudo apt install qemu-system-x86 qemu-utils` |
| Fedora / CentOS | `sudo dnf install qemu-system-x86` |
| macOS | `brew install qemu` |
| Windows | `winget install qemu`, or download the single-file installer from qemu.org |

Verify after installing:

```sh
qemu-system-i386 --version
```

## Two commands

```sh
cmake -B build && cmake --build build
qemu-system-i386 -kernel build/kernel -hda build/pomelo.img -serial stdio
```

The first produces two files: `build/kernel` (the kernel) and `build/pomelo.img` (the disk image). The second: QEMU acts as the bootloader and loads the ELF, while attaching the image as the primary IDE disk and forwarding the serial port to the current terminal. `make run` (a custom target in CMake) is equivalent to the second one. To see the real boot screen, drop `-serial stdio`: QEMU opens a window, and keyboard input goes through an emulated PS/2 — a different road from the serial port.

No machine model is named in the command, so the default is `pc` — the primary IDE disk hangs off this machine's PIIX3. Do not deliberately add `-M q35`: that machine's disks go through AHCI, the ATA ports sit idle, and the fs service prints one line and then stands still. That is a designed degradation, not a hang; see ata.c in [Chapter 8](/guide/fs-server).

## Network: one e1000, plugged into the user-mode network

`make run` and the two smoke scripts add three arguments to QEMU:

```sh
-netdev user,id=n0,hostfwd=tcp::2323-:2323 \
-device e1000,netdev=n0 \
-object filter-dump,id=f0,netdev=n0,file=net.pcap
```

- `-netdev user`: qemu's built-in SLIRP user-mode network — no root, it never touches the host's network card, everything is emulated inside the qemu process. The addresses are fixed by convention: the guest gets 10.0.2.15 and the gateway 10.0.2.2; `hostfwd=tcp::2323-:2323` forwards the host's port 2323 into the guest's 2323, for the netsh of later chapters.
- `-device e1000`: one 82540EM network card. The net task probes it at boot, reads the MAC, sets up its receive and transmit rings, and sends an ARP request — the line "net: e1000 up" and the first frame in the pcap are all the evidence there is that the cable is live.
- `-object filter-dump`: writes the frames on this network card as they are, into `net.pcap`, so on the host `tcpdump -nn -r net.pcap` can inspect them frame by frame. SLIRP frames never leave the host's network card, so capture can only go through this file; tcpdump on the real interface is no use.

## Smoke tests: the ones CI runs too

Whether the build products are right, QEMU gets the final say. `tools/smoke.sh` starts a headless QEMU and types a string of commands for you over the serial port (`help`, `ps`, `spawn probe`, `ls`, `cat hello.txt`, `uptime`, `echo`, `mem`), then scores them one by one — the boot banner, the e1000 coming online, netsh's listening announcement, each command's reply, the help listing checked against the command table, probe's two deaths and two lives. Any single miss fails the whole round. `tools/walkthrough.sh` goes further — a full extra round-trip of write/ls/cat on a newly created file; it is the complete walkthrough that nightly runs every night:

```sh
ctest --test-dir build          # or: cmake --build build -t test
```

The tests are **pace-driven**: one second between each command — tty handles only a few characters per interrupt, and bursty input blows up its line buffer. Let it run on its own once before you power on real hardware; that habit pays for itself.

`tools/smoke-ps2.sh` tests the other input road: the serial port is attached to null and nothing feeds it; QEMU's `sendkey` types a `mem` on the emulated PS/2 keyboard, and then the reply is dug out of the VGA text buffer at `0xb8000` — the keyboard interrupt, the scan-code table, the screen echo, all of it is in this one test. `tools/smoke-mouse.sh` carries the same forensics over to the PS/2 mouse: the monitor sends `mouse_move` / `mouse_button`, and what is verified is the attribute byte of the cursor cell (`0x70` is the cursor, `0x07` is the cell it restores after leaving). `tools/smoke-net.sh` is the network's turn: it starts its own QEMU with `hostfwd` (the host's 2323/2325 reachable), first puts up an ear in the form of `nc -l -p 2324` on the host (the OpenBSD variant; `-N` half-closes once sending is done, otherwise the echo never sees EOF), then checks four things — two TCP echoes, the second one specifically exercising the "connection ends, reopen the listener" transition path; one UDP echo; the greeting really reaching the host; and the two TCP connections each counting one `peer closed`. This ear is itself a hard-won lesson: a leftover `nc` from a previous failed run steals the next round's greeting, so in the trap QEMU and the ear live and die together — cleaning up after yourself is part of the test.

Two more commands in the battery say nothing, and each gets its own test that proves **their silence**: `tools/smoke-clear.sh` types a `mem`, snapshots `0xb8000`, then types `clear` and snapshots again — the second screenshot shows nothing but the redrawn prompt, the boot banner and the reply just now both gone. `clear` does not utter a single character on the serial port (it is a message sent to tty; tty answers one `SYS_CLEAR` and wipes the video memory), so the evidence can only be "wiped clean" and "after the wipe, tty is still alive". `tools/smoke-reboot.sh` is even more decisive: QEMU is started with `-no-reboot`, and after typing `reboot` it is supposed to exit on its own (status 0); if it just sits there until `timeout` strangles it (status 124), then that one stroke `0x64 <- 0xfe` never actually brought the machine down. The serial log only proves that the `rebooting...` line got printed; whether the machine actually stopped is told by the exit status.

## nightly: a real run every night

The smoke tests only run `pc`. The repository's [nightly workflow](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml) does a more complete round of evidence collection every night: build the kernel, the image and the ISO, then **boot both machines, q35 and pc, and collect evidence from each** — first run `tools/walkthrough.sh` through the complete command battery and capture the serial log, then use the monitor's `screendump` to grab the boot screen — and attach the ISO, the kernel, the disk image, together with each machine's own `serial-q35.log`, `boot-q35.png`, `serial-pc.log`, `boot-pc.png`, to the rolling [nightly release](https://github.com/cuihairu/pomelo/releases/tag/nightly). Artifact names must carry the machine tag: two same-named assets in one upload and GitHub answers 422 straight away. There are two ways to trigger it: the nightly schedule, or manual `workflow_dispatch` (with optional extra QEMU arguments, for example `-d int` to watch interrupts). The evidence-collection script is `tools/nightly-run.sh`; the pass/fail criteria for the two machine variants are written in the file header — q35 may pass with only the shell checks, and a hang counts as failure without exception. The walkthrough's verdict is written at the end of the script: **the walkthrough's verdict is the night's verdict** — if the walkthrough's scoring fails, nightly-run exits as a failure, and unless both arms are green together, the step that refreshes the release never runs at all.

## Cross-compilation? No — compiling natively is enough

The kernel is **freestanding**: no libc, no host libraries linked, so no cross toolchain is needed:

```cmake
target_compile_options(kernel PRIVATE
    -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin
    -nostdlib -Wall -Wextra -O2 -g)
```

- `-m32`: emit 32-bit code (the QEMU pc machine is 32-bit at boot);
- `-ffreestanding`: do not assume a standard library;
- `-fno-pie` / `-fno-stack-protector`: turn off the address randomization and stack protection that host distributions enable by default; both need runtime support, which the kernel does not have.

Linking uses the bundled `boot/kernel.ld` to place the kernel at physical address 1 MB and to keep the multiboot magic number at the very front of the file — QEMU only cares whether that magic number is in the first 8 KB.

## mkfs: laying a disk on the host

`tools/mkfs/mkfs.c` is an ordinary Linux program, has nothing to do with the kernel, and only writes files in the format of [Chapter 7](/guide/disk-format):

```
  write the superblock (1 sector)
  write the inode table (4 sectors, all zeros)
  write the seed files one by one: allocate data blocks → fill → fill in the inode
```

The two seeded files: `/hello.txt` (for `cat`) and `/readme.md` (for `ls`). At build time CMake first compiles mkfs, then runs it to produce the image; the ordering dependency is written into one single command:

```cmake
add_custom_command(OUTPUT ${IMG}
    COMMAND $<TARGET_FILE:mkfs> ${IMG} ${CMAKE_SOURCE_DIR}/tools/mkfs/seed
    DEPENDS mkfs)
```

(`${IMG}` is `build/pomelo.img`; `$<TARGET_FILE:…>` is a CMake generator expression that expands to the real path of mkfs's build product — reference the target, do not hardcode a path.)

## Why the image and the kernel are two files

The kernel enters memory through `-kernel`; the image is attached as a disk through `-hda`; the two know nothing about each other. That exactly reproduces the division of labor in a real system: the bootloader owns code, the block device owns data. Want to change the image's contents? No need to rebuild the kernel. Want to change the kernel? The image stays untouched.

## Common problems

| Symptom | Cause |
| --- | --- |
| Reboots right after power-on | Interrupts enabled too early; see [Chapter 2](/guide/boot) |
| `fs: bad disk` | `-hda` not attached or the image is corrupt; rerun `cmake --build build` |
| Keyboard does not respond | Check whether the serial port produces output, and confirm the tty task was created |
| Mouse does not respond | The `mouse:` line in the serial log is the initialization verdict: no such line means the 8042 handshake failed; a line present but still no movement mostly means arguments like `-usbdevice tablet` knocked the default PS/2 mouse out, and IRQ12 naturally brings no events |

## Files of this chapter

```
CMakeLists.txt         top-level build: kernel, mkfs, image, ISO, the run target, smoke tests
tools/mkfs/mkfs.c      host-side disk image generator
tools/smoke.sh         QEMU smoke test: serial-driven shell, grep verification
tools/walkthrough.sh   real-machine walkthrough: the complete command battery + write/ls/cat round trip, scored line by line
tools/smoke-ps2.sh     QEMU smoke test: PS/2 keyboard input, VGA text-buffer forensics
tools/smoke-mouse.sh   QEMU smoke test: PS/2 mouse events, VGA attribute-byte forensics
tools/smoke-net.sh     QEMU smoke test: hostfwd in and out, the socket service talking to nc
tools/smoke-clear.sh   QEMU smoke test: one VGA screenshot before clear, one after; passes only when wiped clean
tools/smoke-reboot.sh  QEMU smoke test: passes only when QEMU exits on its own after reboot (status 0)
tools/nightly-run.sh   nightly forensics: dual boot, serial logs + screendump
tools/mkiso.sh         package the kernel into a BIOS-bootable ISO (grub multiboot)
boot/kernel.ld         the linker script
```

The next chapter: [The PS/2 mouse](/guide/mouse).
