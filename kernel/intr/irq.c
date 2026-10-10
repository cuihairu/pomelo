#include "intr.h"
#include "../sched/sched.h"

/* irq.c: interrupt ownership. A driver claims a line with irq_install;
 * to wait for events it blocks in irq_wait instead of polling. One
 * waiter per line, plus one bit of memory for events that arrive while
 * nobody waits: an interrupt is never lost, only parked. */

static void (*handlers[16])(void);
static u8  waiters[16];
static u16 pending;

void irq_install(u32 irq, void (*fn)(void)) {
    handlers[irq] = fn;
}

/* Run the line's handler, if anybody owns the line. */
void irq_dispatch(u32 irq) {
    if (handlers[irq]) handlers[irq]();
}

/* A claimed line belongs to a ring 3 driver, and on a level-triggered
 * pic input that driver must quiet the device before the line is
 * re-armed -- so the kernel leaves the eoi to it (sys_irq_eoi). Lines
 * nobody claimed are kernel business all the way: acked on the spot. */
int irq_owned(u32 irq) {
    return handlers[irq] != 0;
}

/* Called from interrupt context: wake the waiter if any, else remember. */
void irq_raise(u32 irq) {
    if (waiters[irq]) {
        sched_wake(waiters[irq]);
        waiters[irq] = 0;
    } else {
        pending |= 1u << irq;
    }
}

/* Block until any IRQ in the mask fires. Returns early for two other
 * reasons, and both matter: a message arrived (the caller must pump its
 * inbox), or the inbox is non-empty right now. That last check closes a
 * race -- a task preempted between draining its inbox and calling us
 * could otherwise fall asleep with undelivered mail, and the sender's
 * message would sit there forever. Never sleep with unread mail. */
void irq_wait(u32 mask) {
    for (;;) {
        intr_disable();
        if (tasks[cur].inbox.count) {
            intr_enable();
            return;
        }
        if (pending & mask) {
            pending &= ~mask;
            intr_enable();
            return;
        }
        for (u32 irq = 0; irq < 16; irq++)
            if (mask & (1u << irq)) waiters[irq] = cur;
        tasks[cur].woke = 0;
        tasks[cur].state = ST_BLOCKED;
        sched_next();
        if (tasks[cur].woke) { tasks[cur].woke = 0; intr_enable(); return; }
    }
}
