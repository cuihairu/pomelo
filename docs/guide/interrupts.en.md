> [Chinese](interrupts.md) | **English**

# 03 · Interrupt dispatch

Corresponding code: `kernel/intr/`

## Interrupts are the reason the kernel exists

Without interrupts, the kernel is just a piece of code that is called once and then sleeps. Interrupts push "something happened out there" into the CPU, and only then does the kernel get a chance to react. In pomelo's kernel, only two things bear directly on interrupts: **confirm the source** and **hand the event to the task waiting on it**.

## The IDT: a door-plate table of 256 entries

When the CPU receives interrupt number `n`, it looks up the `n`th entry of the IDT and jumps to the corresponding handler entry. We only need two kinds of gate: exception (divide by zero, page fault) and hardware interrupt. `kernel/intr/idt.c` fills every entry with an assembly stub:

```c
void idt_set(int n, u32 handler, u16 sel, u8 flags) {
    idt[n].offset_lo = handler & 0xffff;
    idt[n].selector  = sel;
    idt[n].zero      = 0;
    idt[n].flags     = flags;          /* 0x8E = an existing 32-bit interrupt gate */
    idt[n].offset_hi = handler >> 16;
}
```

`stubs.S` writes one few-line stub for each **actually used** vector, all of them pushing the vector number and then jumping to a common entry:

```asm
.macro STUB vec
intr_stub_\vec:
    pushl $0                  # some exceptions carry their own error code; plain interrupts push 0 to align
    pushl $\vec
    jmp   intr_common
.endm

STUB 32                       # clock (PIT)
STUB 33                       # keyboard
STUB 36                       # serial port
STUB 44                       # mouse (IRQ12)
STUB 128                      # the syscall gate

# 13 and 14 get their error code pushed by the CPU, so the stubs push no 0 (see below)
intr_stub_13:
    pushl $13
    jmp   intr_common
intr_stub_14:
    pushl $14
    jmp   intr_common
```

CPU exceptions (numbers 0–31) are not decoded one by one for the most part: an exception in kernel mode means something big has gone wrong, so they share one stub, print one line of PANIC and halt. The two exceptions are 13 (general protection) and 14 (page fault) — they carry their own error codes, and the fault may have been caused by a ring 3 task, so each has to be investigated before deciding who to kill; see [Chapter 14](/guide/ring3.en). `intr_common` saves the scene into a `regs` structure and hands it to the C dispatch function.

## Three bits on the gate: 0x8E, 0xEE, and one cli

The flags byte of the gate descriptor is worth reading bit by bit. This kernel uses two of them:

- `0x8E`: present, DPL=0, 32-bit **interrupt gate** — exceptions and all hardware interrupts.
- `0xEE`: the same, but DPL=3. In the whole table only this one gate, `int 0x80`, can be knocked on from ring 3.

The type is always 0xE (interrupt gate): when the CPU crosses the threshold it **clears IF by itself**. So user code can neither touch the hardware gates (DPL=0, knock and you get #GP) nor enter the kernel with interrupts still on.

The real story of interrupt state is at the **exit**. Every primitive that can sleep — `ipc_recv`, `irq_wait` — reopens IF when it wakes: a driver that just got the wake-up and goes back to ring 3 still has to keep being interrupted, otherwise during the few microseconds it spends clearing its inbox the clock does not tick and the serial port stays silent; and no task should carry a stale `cli` back into user mode either. So when a handler returns from `intr_dispatch`, whether IF is on depends on whether it ever slept.

And the tail of `intr_common` has to take the scene apart from the stack: `popa` reads the registers back, `iret` pops EIP/CS/EFLAGS. If IF happens to be on at that moment, one clock tick lands exactly between the `call` returning and the `popa`, and its interrupt frame gets pressed straight into the middle of the register enclave — the registers this task wakes up holding belong to someone else, and in the end `iret` walks the wrong stack and slams into #GP on its own address. We really did step in this pitfall: before it was fixed, a freshly burned image hit a "torn" interrupt frame in its very first round of smoke-net.

The fix is not to change every primitive's exit to disable interrupts (that would spread the problem to every spot), but to light one `cli` at the top of `intr_common`: **the few instructions that take the scene apart are forever atomic**. The price is one extra instruction per interrupt; the payoff is that the correctness of the tail no longer depends on any handler's bedtime habits. `iret` restores EFLAGS from the frame, so this cli does not leak into ring 3. One sentinel was installed too: before `iret` it checks whether the CS about to be popped is one of the two this kernel issues (`0x08` / `0x1b`); if not, some earlier write has reached this frame. `sched_seat` recognizes from the stack address which seat the frame lands in, and `torn_frame` spreads the raw frame together with the seat number out on the serial port before halting — if the same problem comes again, the scene comes with it.

## Dispatch: answer first, then wake

```c
void intr_dispatch(struct regs *r) {
    if (r->int_no == 0x80) {          /* 0x80 = the syscall gate */
        syscall_entry(r);
        return;
    }
    if (r->int_no == 14) {             /* page fault */
        task_fault(r, "page fault", cr2_fault());
        return;
    }
    if (r->int_no == 13) {             /* general protection */
        task_fault(r, "general protection fault", 0);
        return;
    }
    if (r->int_no >= 32 && r->int_no < 48) {
        u32 irq = r->int_no - 32;
        irq_dispatch(irq);            /* whoever claimed this line runs its three lines first */
        if (!irq_owned(irq))
            pic_eoi(irq);             /* the kernel's own line: take it on the spot */
        irq_raise(irq);               /* then wake the task waiting on it */
        if (irq == IRQ_PIT) {
            char_retry();             /* input the tty queue did not take, try once more */
            sched_clock_tick();       /* heartbeat + preemption */
        }
    }
}
```

The ownership of the EOI follows the ownership of the line: lines the kernel keeps for itself — keyboard, mouse, clock — the kernel EOIs on the spot; lines claimed by user-mode drivers (the network card kind), the kernel **deliberately does not send it** — under level triggering you have to shut the device up first and then re-arm the 8259, and only the driver holding the hardware knows how to shut it up; the driver clears the device and then goes through `sys_irq_eoi`. Reverse the order and the 8259 immediately delivers the same level again, turning one interrupt into a storm.

One more word on the PIC remap (`pic_remap`): by default IRQ0–15 map to vectors 0–15, which collide with the CPU exceptions (0–31). We shift the whole range to 32–47 — well water and river water each keep to themselves.

`pic_remap` has one more trick: it opens only the lines **someone has claimed** (clock, keyboard, serial, cascade) and masks all the rest. This is not fussiness — an open interrupt line with no one managing it lets a signal walk straight through an empty IDT gate and flip the kernel with #GP on the spot. We paid for this one while debugging the disk driver: the moment IRQ14's first command completed, the system went into an exception, error code `0x172` = 46×8+2 — exactly the signature of an "empty gate".

## A small mechanism: irq_wait, turning interrupts into "waitable events"

A driver should not do work inside the interrupt handler. The real arrangement is: the interrupt only does the "wake-up", and the task that does the work waits elsewhere. The kernel provides the primitive:

```c
void irq_wait(u32 mask);   /* block until any IRQ in mask reports */
```

The first user is the network card driver ([Chapter 16](/guide/net.en)): its main loop masks "its own interrupt line + the clock" together into the mask and sleeps — woken by the device it collects packets, woken by the clock it checks timeouts; one wait, two alarms. What sleeps on one line is not one person but **a row of seat bits**: one bit per seat, and when the interrupt arrives the whole row is woken together; only a completely empty line records the event as "pending" and docks it for the next passerby. (The keyboard and serial port do not go this way — their interrupts are turned directly into messages in kernel/char.c and tossed to the tty; see [Chapter 9](/guide/tty-server.en).)

`irq_wait` has two early-return exits: a message arrived (the caller has to go handle the inbox), or — the inbox **right now holds an unread message**. The second was bought with blood and tears: if a task gets cut off by the clock between the two steps "empty the inbox" and "go to sleep", it will wake up and sleep to death still holding unread letters, and the sender can only wait in vain. The check and the sleep must be done in one breath with interrupts disabled — **never go to sleep with unread letters**. This way there is no extra logic in the interrupt handler, and "wait for the device" looks like one ordinary blocking call in the service's eyes — in a microkernel, even interrupts are abstracted into waitable objects.

## Files in this chapter

```
kernel/intr/intr.h     IRQ primitives and the regs structure
kernel/intr/idt.c      filling and loading the IDT
kernel/intr/stubs.S    one stub per actually used vector
kernel/intr/intr.c     pic_remap / pit_init / intr_dispatch / torn_frame / panic
kernel/intr/irq.c      claim table, waiters set, pending bits and irq_wait
```

The next chapter: [Scheduling](/guide/scheduling.en).
