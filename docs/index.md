---
layout: home

hero:
  name: Pomelo
  text: 一个 i386 教学用的简易微内核
  tagline: C 实现的 i386 小内核 · 边读代码边学操作系统 · 每个目录就是一个概念 · 教学用途,非生产可用
  actions:
    - theme: brand
      text: 开始阅读
      link: /guide/intro
    - theme: alt
      text: 本地跑起来
      link: /guide/build-and-run
    - theme: alt
      text: 下载 nightly
      link: https://github.com/cuihairu/pomelo/releases/tag/nightly

features:
  - title: 微内核,只有五件事
    details: 内核里只有中断分发、调度、IPC、系统调用门和分页;文件系统、终端、shell 全部是跑在消息传递上的独立任务,shell 还住在自己 ring 3 的地址空间里。
  - title: 目录即教材目录
    details: boot/ 讲启动,kernel/ 讲内核五件事,servers/ 与 apps/ 讲服务。章节顺序就是代码目录顺序,每段代码都对应一个操作系统概念。
  - title: 两条命令跑起来
    details: cmake -B build && cmake --build build,QEMU 直接引导,不需要装 GRUB,不需要做 ISO。
---
