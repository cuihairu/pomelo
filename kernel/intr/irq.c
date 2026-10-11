#include "intr.h"
#include "../sched/sched.h"

/* irq.c: interrupt ownership. Two different kinds of owner, and the
 * eoi split below turns on which one is meant. A kernel-internal isr is
 * installed with irq_install and runs in interrupt context, so it can
 * never call sys_irq_eoi -- the kernel acks such a line on the spot. A
 * ring 3 driver claims a line through sys_irq_enable and waits on it
 * instead of polling; a line sleeps a set of waiters -- one bit per
 * seat, so two drivers sharing a line wake together instead of the
 * second overwriting the first into a sleep nobody will ever end --
 * plus one bit of memory for events that arrive while nobody waits:
 * an interrupt is never lost, only parked. */

static void (*handlers[16])(void);
static u16 waiters[16];     /* per line: the seats asleep on it */
static u16 pending;
static u16 claimed;

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
 * nobody claimed are kernel business all the way: acked on the spot.
 * (irq_install does not mean claimed: the keyboard and serial isrs sit
 * in handlers[] too, but they run in interrupt context and have no way
 * to reach sys_irq_eoi -- treating them as owners would strand the pic
 * in-service bit and silence the line after its first event.) */
int irq_owned(u32 irq) {
    return (claimed >> irq) & 1u;
}

/* The claim act: a ring 3 driver opens its line through sys_irq_enable,
 * and from then on it -- not the kernel -- owns the line's eoi. */
void irq_claim(u32 irq) {
    claimed |= 1u << irq;
}

/* Called from interrupt context: wake everyone asleep on the line, or,
 * when the line has no waiters, remember the event for the next one
 * through. sched_wake ignores seats that are not blocked, so a stale
 * bit -- a waiter roused by mail before its irq came -- costs nothing. */
void irq_raise(u32 irq) {
    u16 set = waiters[irq];
    waiters[irq] = 0;
    if (!set) {
        pending |= 1u << irq;
        return;
    }
    for (int t = 0; t < NTASK; t++)
        if (set & (1u << t)) sched_wake(t);
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
            if (mask & (1u << irq)) waiters[irq] |= 1u << cur;
        tasks[cur].woke = 0;
        tasks[cur].state = ST_BLOCKED;
        sched_next();
        /* The wait is over, one way or another. Our seat claims every
         * line in the mask again only next round around, so a line that
         * fired for another waiter cannot drag us out of a later sleep
         * -- irq_raise clears the whole set when it fires, but the mail
         * path leaves our bit standing. */
        for (u32 irq = 0; irq < 16; irq++)
            if (mask & (1u << irq)) waiters[irq] &= ~(1u << cur);
        if (tasks[cur].woke) { tasks[cur].woke = 0; intr_enable(); return; }
    }
}
