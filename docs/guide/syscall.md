# 06 · The syscall gate

Corresponding code: `kernel/syscall/`

## Why services need a gate

A user-mode task that wants to send a message, that wants to block, or that wants to reboot the machine has to ask the kernel to do it for it — because all of these need **kernel privilege**: writing the task table, touching the queues, reaching the hardware. The channel for asking the kernel is the syscall.

In a real microkernel, this channel is guaranteed by the CPU privilege levels plus the far-gate hardware. Pomelo's service tasks lived entirely in ring 0 in the early days, but the **interface** was followed from day one: a software interrupt gate, `int 0x80`, which formalizes the act of "entering the kernel." When the tasks move into ring 3 in [Chapter 14](/guide/ring3), this gate together with `DPL=3` becomes the only channel from user to kernel — the interface does not change, a lock is just hung on the door.

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
    SYS_MMIO    = 13,  /* ebx=pa: map a page of device registers, returns a virtual address
                        * (pa below the 16 MiB span is refused: that is RAM, not a device) */
    SYS_IRQ_EOI = 14,  /* ebx=irq: re-acknowledge the line you took over */
};
```

On the kernel side it dispatches by number — but every pointer that crosses the door is checked first. Arguments travel in registers (eax = number, ebx/ecx = arguments), and the return value is written back to eax. The check is one helper, `user_ok`: the range must sit in the user region and every page it touches must be mapped in the caller's own directory, or a bad pointer would fault the *kernel* instead of the task that passed it (`kernel/syscall/syscall.c:26-35`):

```c
static int user_ok(const void *p, u32 len) {
    u32 a = (u32)p, b = a + len;
    if (b < a) return 0;                       /* wrapped around */
    if (a < USER_BASE || b > USER_STACK_TOP) return 0;
    while (a < b) {
        if (!v2p(a)) return 0;                 /* a hole: page not mapped */
        a = (a & ~0xFFFu) + 4096;
    }
    return 1;
}
```

```c
void syscall_entry(struct regs *r) {
    switch (r->eax) {
    case SYS_YIELD:
        sched_tick();                 /* rotate: hand the CPU to the next one */
        r->eax = 0;
        return;
    case SYS_SEND:
        if (!user_ok((void *)r->ecx, sizeof(struct msg))) { r->eax = -1; return; }
        r->eax = ipc_send(r->ebx, (struct msg *)r->ecx);
        return;
    case SYS_RECV:
        if (!user_ok((void *)r->ebx, sizeof(struct msg))) { r->eax = -1; return; }
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
        /* ecx is unsigned here; a huge length fails the user_ok range
         * check on the wrap instead of overflowing the console loop. */
        if (!user_ok((const void *)r->ebx, r->ecx)) { r->eax = -1; return; }
        con_write((const char *)r->ebx, r->ecx);   /* the console is the kernel's to run */
        r->eax = 0;
        return;
    case SYS_CLEAR:
        con_clear();
        r->eax = 0;
        return;
    case SYS_SPAWN:
        /* sched_spawn reads at most PROG_NAME bytes of the name. */
        if (!user_ok((const void *)r->ebx, PROG_NAME)) { r->eax = -1; return; }
        r->eax = sched_spawn((const char *)r->ebx);
        return;
    case SYS_PS: {
        /* ps_snapshot writes at most NTASK-1 rows, whatever max says. */
        u32 want = (u32)r->ecx;
        if (want > NTASK) want = NTASK;
        if (!user_ok((void *)r->ebx, want * sizeof(struct ps_entry))) {
            r->eax = -1;
            return;
        }
        r->eax = ps_snapshot((struct ps_entry *)r->ebx, r->ecx);
        return;
    }
    case SYS_UPTIME:
        r->eax = sched_uptime();
        return;
    case SYS_MEM:
        if (!user_ok((void *)r->ebx, sizeof(struct mem_info))) {
            r->eax = -1;
            return;
        }
        frame_stats((struct mem_info *)r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_ENABLE:
        /* The pic mask and the idt are kernel territory; the driver that
         * owns the line asks here. The stub is empty by design: waking
         * the waiter is irq.c's job, and the driver clears the device's
         * own interrupt state once it runs. Claiming the line is what
         * moves its eoi from the kernel to the driver (sys_irq_eoi).
         * irq_no_ok first: irq.c indexes its tables by the line number,
         * and a bogus one would shift past the mask (1u << 16+) and poke
         * pic ports that belong to nobody. */
        if (!irq_no_ok(r->ebx)) { r->eax = -1; return; }
        pic_irq_enable(r->ebx);
        irq_install(r->ebx, net_irq_stub);
        irq_claim(r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_EOI:
        /* The other half of owning a line: the driver quieted its device,
         * now the pic may re-arm it. The kernel skipped the eoi on
         * delivery exactly so this could happen in the right order.
         * The range check matters as much as it does above: pic_eoi would
         * happily ack a line 16+ (or a slave ack for 8-15 that never fired)
         * and strand in-service bits. */
        if (!irq_no_ok(r->ebx)) { r->eax = -1; return; }
        pic_eoi(r->ebx);
        r->eax = 0;
        return;
    case SYS_V2P:
        r->eax = v2p(r->ebx);
        return;
    case SYS_MMIO:
        r->eax = mmio_map(r->ebx);      /* refuses pa below the 16 MiB span */
        return;
    }
    r->eax = -1;                      /* no such number */
}
```

## The calling convention on the user side

A service task cannot call `ipc_send` directly; instead it goes through one shared `gate` helper that stuffs the arguments into registers and triggers `int 0x80`, and every syscall number gets a one-line wrapper over it:

```c
/* User-side: enter the kernel through the int 0x80 gate only. */
static inline i32 gate(int n, int a, int b) {
    i32 ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}
static inline int  sys_send(int dst, struct msg *m) { return gate(SYS_SEND, dst, (int)m); }
static inline int  sys_recv(struct msg *m)          { return gate(SYS_RECV, (int)m, 1); }
...
static inline u32  sys_mmio(u32 pa)                 { return gate(SYS_MMIO, (int)pa, 0); }
...
static inline void sys_send_wait(int dst, struct msg *m) {
    while (sys_send(dst, m) < 0) sys_yield();      /* a full queue is retried, not dropped */
}
```

(The `...` cuts are the other thirteen numbers — each one the same one-liner — followed by `sys_send_wait`, the retry loop every request/reply caller uses.)

`int 0x80` makes the CPU jump to entry 128 of the IDT — we wire it to `intr_stub_128` inside `idt_init`. The stub pads in a fake error code, pushes the vector number, and jumps into `intr_common`, which saves the `regs` scene and hands off to `intr_dispatch`; only when it recognizes 0x80 does it pass on to `syscall_entry`. This path shares the same stub machinery as the [interrupts chapter](/guide/interrupts); only the entry point is different.

## One door, not a pile of functions

Worth emphasizing: between a Pomelo task and the kernel there are **only these fifteen calls**. Reading a file, a keystroke — none of it goes through the syscall, but is a message between services (via `SYS_SEND`/`SYS_RECV`); `SYS_WRITE`/`SYS_CLEAR` are the only exceptions — the screen is the kernel's to run, and a task that wants to say anything has only this one exit (the reason is in [Chapter 14](/guide/ring3)). The latest four (11–14) were added when the NIC driver moved into ring 3: unmask an interrupt line, acknowledge receipt, the physical address for the DMA descriptor, mapping the device registers — for the privileges a driver asks for, the kernel never hands one over for free, one gate at a time. The smaller the syscall table, the smaller the kernel; the smaller the kernel, the closer it comes to what a microkernel is supposed to be.

## Files of this chapter

```
kernel/syscall/syscall.h   call numbers and the user-mode inline wrappers
kernel/syscall/syscall.c   the int 0x80 entry and the dispatch table
```

The next chapter enters the world of user mode: [the disk format](/guide/disk-format).
