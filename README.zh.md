# Pomelo

> 用 C 从零写的微内核——小到一次读完。

Pomelo 是一个 i386 平台的教学内核,一个**逻辑微内核**:内核只做五件事
(中断分发、调度、IPC、系统调用门、分页)。除此之外的一切——文件系统、终端、
shell——都是跑在消息传递之上的独立任务,而且每一个都住在自己 ring 3 的
地址空间里。它用 QEMU 直接启动,不需要安装引导器。

[![nightly](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml/badge.svg)](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml)

[English](README.md)

## 快速开始

```sh
cmake -B build && cmake --build build
ctest --test-dir build          # 在 QEMU 中启动,校验启动横幅
```

## 本地跑起来

QEMU 是唯一的运行依赖:

| 平台 | 安装 |
| --- | --- |
| Ubuntu / Debian | `sudo apt install qemu-system-x86 qemu-utils` |
| Fedora / CentOS | `sudo dnf install qemu-system-x86` |
| macOS | `brew install qemu` |
| Windows | `winget install qemu` |

`qemu-system-i386 --version` 确认装好,然后选一种模式:

```sh
# 串口模式:shell 就在你的终端里(退出:先按 Ctrl+A,再按 X)
qemu-system-i386 -M pc -kernel build/kernel -hda build/pomelo.img -nographic

# 显示模式:开一个真窗口,键盘走模拟 PS/2
qemu-system-i386 -M pc -kernel build/kernel -hda build/pomelo.img
```

`-M pc` 不是可有可无:默认的 `q35` 机型把盘挂在 AHCI 后面,这块 ATA 驱动够不着,
fs 服务会打印一行提示,之后对每个请求都回一句 `fs: no disk`,shell 照常能用。[nightly workflow](https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml)
每天把两种机型各真跑一遍,串口记录和启动截图都挂在 [nightly release](https://github.com/cuihairu/pomelo/releases/tag/nightly) 里。

## 目录

每个目录对应一个概念,教材章节顺序与目录顺序一致。这道分界线本身就是重点:
`kernel/` 只放每个任务都绕不开的五个机制,`servers/` 住着用户态服务,应用想
办事只能发消息。内核不解析磁盘格式;文件系统放在内核外面,磁盘上的 bug
想伤到内核,得先挤过那扇 IPC 的门。崩掉的服务也按出生的方式回来:shell 敲
`spawn` 按名字请一遍,内核把它建回老座位——任务号不变,发往它的消息照走。
内核唯一拥有的硬件是控制台:键盘和
串口的中断落在 `kernel/char.c`,由它把每个字符变成一条消息投给 tty 服务;
输出则反过来,一个系统调用就够。

```
boot/       从上电到第一行 C:multiboot、GDT、IDT、PIT。
kernel/     微内核本体:中断、调度、IPC、系统调用、分页,外加喂 tty 的控制台
            硬件(char.c)。
servers/fs/ 文件服务,以及它那套极简磁盘格式(超级块 + inode)。
servers/tty/ 终端服务:内核送来的字符攒成行。
apps/shell/ 命令行 REPL,通过 IPC 与各服务对话(跑在 ring 3)。
tools/mkfs/ 宿主机工具,负责铺好磁盘镜像。
docs/       教材(VitePress),一章对应上面一个目录。
```

## 文档

完整教材在 `docs/`,用 VitePress 构建:

```sh
npm --prefix docs install
npm --prefix docs run dev
```

## 许可

MIT
