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
static int claimed_by[16] = {   /* which seat claims the line; -1 = none */
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
};

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
 * and from then on it -- not the kernel -- owns the line's eoi. The seat
 * is remembered so a crash can give the line back (irq_release).
 *
 * A claim is exclusive, and two doors are shut. A kernel line (the
 * clock, the keyboard, the serial port, the mouse, the cascade) is
 * never handed out -- the kernel services it in interrupt context and
 * has no way to reach sys_irq_eoi, so a claimed kernel line would
 * strand its in-service bit and go silent after one event. And a line
 * another seat already holds is not taken over either: two drivers on
 * one line would mean two eois per event, or one -- and the loser's
 * death (irq_release re-masks the line) would cut the winner off its
 * own hardware. Re-claiming a line this seat already owns is a no-op,
 * so a driver that re-runs its bringup stays put. */
int irq_claim(u32 irq) {
    if (irq_kernel(irq)) return -1;
    if (((claimed >> irq) & 1u) && claimed_by[irq] != cur) return -1;
    claimed |= 1u << irq;
    claimed_by[irq] = cur;
    return 0;
}

/* The finer question sys_irq_eoi asks: not just "is this line claimed",
 * but "is it claimed by the caller". An eoi from anyone else would
 * re-arm a line its owner may still be quieting. */
int irq_owned_by(u32 irq, int seat) {
    return ((claimed >> irq) & 1u) && claimed_by[irq] == seat;
}

/* Which line this seat owns, or -1 when it owns none. SYS_V2P asks so
 * it can stamp the frames a driver hands to a card: a machine address
 * given out to a task that holds no line is not a DMA target. */
int irq_line_of(int seat) {
    for (u32 irq = 0; irq < NIRQ; irq++)
        if (claimed_by[irq] == seat) return (int)irq;
    return -1;
}

/* A driver that dies takes its hardware with it -- or would: the stub it
 * installed sits in handlers[], the claimed bit keeps the kernel from
 * ever eoi-ing the line, and a device left asserting stays stuck
 * in-service forever. Walk the claimed lines back to the kernel: drop the
 * handler, unclaim, re-mask (the device may still be asserting) and ack
 * the exact line so a stuck in-service bit does not strangle the next
 * driver that claims it. A line another live seat owns is not touched. */
void irq_release(int tid) {
    for (u32 irq = 0; irq < NIRQ; irq++) {
        if (claimed_by[irq] != tid) continue;
        handlers[irq] = 0;
        claimed &= ~(1u << irq);
        claimed_by[irq] = -1;
        waiters[irq] &= ~(1u << tid);
        pending &= ~(1u << irq);
        pic_irq_disable(irq);
        pic_eoi_specific(irq);
    }
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
