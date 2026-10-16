> [Chinese](syscall.md) | **English**

# 06 · The syscall gate

Corresponding code: `kernel/syscall/`

## Why services need a gate

A user-mode task that wants to send a message, that wants to block, or that wants to reboot the machine has to ask the kernel to do it for it — because all of these need **kernel privilege**: writing the task table, touching the queues, reaching the hardware. The channel for asking the kernel is the syscall.

In a real microkernel, this channel is guaranteed by the CPU privilege levels plus the far-gate hardware. Pomelo's service tasks lived entirely in ring 0 in the early days, but the **interface** was followed from day one: a software interrupt gate, `int 0x80`, which formalizes the act of "entering the kernel." When the tasks move into ring 3 in [Chapter 14](/guide/ring3.en), this gate together with `DPL=3` becomes the only channel from user to kernel — the interface does not change, a lock is just hung on the door.

## A fifteen-line dispatch table

```c
enum {
    SYS_YIELD   = 0,   /* yield the CPU */
    SYS_SEND    = 1,   /* send a message */
    SYS_RECV    = 2,   /* receive a message (may block) */
    SYS_IRQ_WAIT= 3,   /* wait on some IRQ */
    SYS_REBOOT  = 4,   /* reboot */
    SYS_WRITE   = 5,   /* ebx=buf, ecx=len: console output, kernel side */
    SYS_CLEAR   = 6,   /* clear the screen */
    SYS_SPAWN   = 7,   /* ebx=name: start a registered program by name */
    SYS_PS      = 8,   /* ebx=buf, ecx=max: a snapshot of the task table */
    SYS_UPTIME  = 9,   /* seconds since boot: the heartbeat answered back */
    SYS_MEM     = 10,  /* ebx=&mem_info: the frame bitmap, counted once */
    SYS_IRQ_ENABLE = 11, /* ebx=irq: unmask the PIC line, take this one over */
    SYS_V2P     = 12,  /* ebx=va: virtual to physical, for the DMA descriptor */
    SYS_MMIO    = 13,  /* ebx=pa: map a page of device registers, returns a virtual address */
    SYS_IRQ_EOI = 14,  /* ebx=irq: re-acknowledge the line you took over */
};
```

On the kernel side it simply dispatches by number. Arguments travel in registers (eax = number, ebx/ecx = arguments), and the return value is written back to eax:

```c
void syscall_entry(struct regs *r) {
    switch (r->eax) {
    case SYS_YIELD:
        sched_tick();                 /* rotate: hand the CPU to the next one */
        r->eax = 0;
        return;
    case SYS_SEND:
        r->eax = ipc_send(r->ebx, (struct msg *)r->ecx);
        return;
    case SYS_RECV:
        r->eax = ipc_recv((struct msg *)r->ebx, r->ecx);
        return;
    case SYS_IRQ_WAIT:
        irq_wait(r->ebx);             /* ebx is the IRQ mask */
        r->eax = 0;
        return;
    case SYS_REBOOT:
        reboot_now();                 /* a tap on port 0x64, reboot */
        return;
    case SYS_WRITE:
        con_write((const char *)r->ebx, r->ecx);   /* the console is the kernel's to run */
        r->eax = 0;
        return;
    case SYS_CLEAR:
        con_clear();
        r->eax = 0;
        return;
    case SYS_SPAWN:
        r->eax = sched_spawn((const char *)r->ebx);
        return;
    case SYS_PS:
        r->eax = ps_snapshot((struct ps_entry *)r->ebx, r->ecx);
        return;
    case SYS_UPTIME:
        r->eax = sched_uptime();
        return;
    case SYS_MEM:
        frame_stats((struct mem_info *)r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_ENABLE:
        /* The PIC mask and the IDT are both kernel territory; a driver that
         * takes a line opens its hole here. The hook that is installed is
         * empty: the one who wakes up is irq.c, and the device's own interrupt
         * state is cleared by the driver. Taking the line over moves this
         * line's EOI from the kernel to the driver (sys_irq_eoi): under level
         * triggering the device has to be made to shut up before the 8259 can
         * be re-acknowledged. */
        pic_irq_enable(r->ebx);
        irq_install(r->ebx, net_irq_stub);
        irq_claim(r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_EOI:
        /* The other half of taking over: the driver has made the device shut
         * up, and only now can the PIC re-acknowledge. The kernel deliberately
         * skips the EOI when it delivers, precisely to leave the ordering to
         * the driver here. */
        pic_eoi(r->ebx);
        r->eax = 0;
        return;
    case SYS_V2P:
        r->eax = v2p(r->ebx);
        return;
    case SYS_MMIO:
        r->eax = mmio_map(r->ebx);
        return;
    }
    r->eax = -1;                      /* no such number */
}
```

## The calling convention on the user side

A service task cannot call `ipc_send` directly; instead it goes through a small wrapper that stuffs the arguments into registers and triggers `int 0x80`:

```c
static inline int sys_send(int dst, struct msg *m) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_SEND), "b"(dst), "c"(m)
        : "memory");
    return ret;
}
```

`int 0x80` makes the CPU jump to entry 128 of the IDT — we wire it to `intr_stub_128` inside `idt_init`. The stub pads in a fake error code, pushes the vector number, and jumps into `intr_common`, which saves the `regs` scene and hands off to `intr_dispatch`; only when it recognizes 0x80 does it pass on to `syscall_entry`. This path shares the same stub machinery as the [interrupts chapter](/guide/interrupts.en); only the entry point is different.

## One door, not a pile of functions

Worth emphasizing: between a Pomelo task and the kernel there are **only these fifteen calls**. Reading a file, a keystroke — none of it goes through the syscall, but is a message between services (via `SYS_SEND`/`SYS_RECV`); `SYS_WRITE`/`SYS_CLEAR` are the only exceptions — the screen is the kernel's to run, and a task that wants to say anything has only this one exit (the reason is in [Chapter 14](/guide/ring3.en)). The latest four (11–14) were added when the NIC driver moved into ring 3: unmask an interrupt line, acknowledge receipt, the physical address for the DMA descriptor, mapping the device registers — for the privileges a driver asks for, the kernel never hands one over for free, one gate at a time. The smaller the syscall table, the smaller the kernel; the smaller the kernel, the closer it comes to what a microkernel is supposed to be.

## Files of this chapter

```
kernel/syscall/syscall.h   call numbers and the user-mode inline wrappers
kernel/syscall/syscall.c   the int 0x80 entry and the dispatch table
```

The next chapter enters the world of user mode: [the disk format](/guide/disk-format.en).
