---
layout: home

hero:
  name: Pomelo
  text: A small i386 microkernel in C for self-study of kernel internals
  tagline: A small i386 kernel in C · Read the code and learn the OS at the same time · Each directory is one concept · For self-study, not production use
  actions:
    - theme: brand
      text: Start reading
      link: /guide/intro
    - theme: alt
      text: Run it locally
      link: /guide/build-and-run
    - theme: alt
      text: Download nightly
      link: https://github.com/cuihairu/pomelo/releases/tag/nightly

features:
  - title: A microkernel with only five jobs
    details: The kernel only has interrupt dispatch, scheduling, IPC, the syscall gate and paging. The filesystem, terminal, shell and network are all standalone tasks that run on top of message passing, each in its own ring 3 address space.
  - title: The directory tree is the curriculum
    details: boot/ covers startup, kernel/ covers the five jobs of the kernel, servers/ and apps/ cover the services. The chapter order is the directory order, and each piece of code maps to one operating system concept.
  - title: Up and running in two commands
    details: cmake -B build && cmake --build build. QEMU boots directly — no GRUB to install, no ISO to make.
---

