# Pomelo

> 用 C 从零写的微内核——小到一次读完。

Pomelo 是一个 i386 平台的教学内核,一个**逻辑微内核**:内核只做四件事
(中断分发、调度、IPC、系统调用门)。除此之外的一切——文件系统、终端、shell——
都是跑在消息传递之上的独立任务。它用 QEMU 直接启动,不需要安装引导器。

[English](README.md)

## 快速开始

```sh
cmake -B build && cmake --build build
ctest --test-dir build          # 在 QEMU 中启动,校验启动横幅
```

## 目录

每个目录对应一个概念,教材章节顺序与目录顺序一致。这道分界线本身就是重点:
`kernel/` 只放每个任务都绕不开的四个机制,`servers/` 住着用户态服务,应用想
办事只能发消息。内核不解析磁盘格式,也不碰键盘;文件系统放在内核外面,
磁盘上的 bug 想伤到内核,得先挤过那扇 IPC 的门。

```
boot/       从上电到第一行 C:multiboot、GDT、IDT、PIT。
kernel/     微内核本体:中断、调度、IPC、系统调用。别的一律不做。
servers/fs/ 文件服务,以及它那套极简磁盘格式(超级块 + inode)。
servers/tty/ 终端服务:扫描码到行缓冲。
apps/shell/ 命令行 REPL,通过 IPC 与各服务对话。
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
