#include "intr.h"
#include "../char.h"
#include "../kprintf.h"
#include "../mm/paging.h"
#include "../sched/sched.h"
#include "../syscall/syscall.h"

/* intr.c: the chip-level half of interrupts -- program the two 8259s,
 * give the scheduler its heartbeat, and route every frame that stubs.S
 * drops on the common path. Ownership (handlers, waiters) is irq.c. */

/* Exceptions 13/14 carry a ring story: a kernel fault is a kernel bug and
 * panics; a ring 3 task that trips the MMU is one task being bad at life,
 * so only it is killed -- the verdict names the faulting address, then the
 * scheduler walks away from a task that will never resume. */
static void task_fault(struct regs *r, const char *what, u32 addr) {
    if ((r->cs & 3) == 0)
        panic(what, r);
    intr_disable();
    kprintf("task %d killed: %s at %x (eip=%x)\n", cur, what, addr, r->eip);
    tasks[cur].state = ST_FREE;
    sched_next();
}

void pic_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);   /* ICW1: init, cascade mode */
    outb(0x21, 0x20); outb(0xA1, 0x28);   /* ICW2: vectors 32..47 */
    outb(0x21, 0x04); outb(0xA1, 0x02);   /* ICW3: slave on IRQ2 */
    outb(0x21, 0x01); outb(0xA1, 0x01);   /* ICW4: 8086 mode */
    /* Open only the lines somebody owns: timer, keyboard, serial and
     * the cascade. An open, unclaimed line vectors through an empty
     * IDT gate and #GPs the kernel -- the disk's IRQ14 does exactly
     * that the moment its first command completes. */
    outb(0x21, (u8)~(1 << IRQ_PIT | 1 << IRQ_KBD | 1 << 2 | 1 << IRQ_COM1));
    outb(0xA1, 0xFF);                     /* no slave lines in use */
}

void pic_eoi(u32 irq) {
    outb(irq >= 8 ? 0xA0 : 0x20, 0x20);
}

/* --- PIT: the timer tick that drives preemption ----------------------- */

void pit_init(u32 hz) {
    u32 div = 1193182 / hz;
    outb(0x43, 0x36);                     /* ch0, lo/hi, square wave */
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);
}

/* --- Dispatch ---------------------------------------------------------- */

void intr_dispatch(struct regs *r) {
    if (r->int_no == 0x80) {              /* syscall gate */
        syscall_entry(r);
        return;
    }
    if (r->int_no == 14) {                /* page fault */
        task_fault(r, "page fault", cr2_fault());
        return;
    }
    if (r->int_no == 13) {                /* general protection */
        task_fault(r, "general protection fault", 0);
        return;
    }
    if (r->int_no >= 32 && r->int_no < 48) {
        u32 irq = r->int_no - 32;
        irq_dispatch(irq);
        pic_eoi(irq);
        irq_raise(irq);
        if (irq == IRQ_PIT) {
            char_retry();             /* input the tty queue could not take */
            sched_clock_tick();       /* heartbeat + preemption */
        }
        return;
    }
    panic("unhandled exception", r);
}

void panic(const char *why, struct regs *r) {
    intr_disable();
    kprintf("\nPANIC: %s", why);
    if (r) kprintf(" (eip=%x int=%x)", r->eip, r->int_no);
    kprintf("\n");
    for (;;) halt();
}
