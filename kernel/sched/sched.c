#include "sched.h"
#include "../intr/intr.h"

struct task tasks[NTASK];
int cur = -1;

static u8 kstacks[NTASK][STACK_BYTES] __attribute__((aligned(16)));

void switch_to(u32 *save_esp, u32 new_esp);   /* switch.S */

void sched_init(void) {
    for (int i = 0; i < NTASK; i++) {
        tasks[i].state = ST_FREE;
        tasks[i].woke = 0;
        msgq_reset(&tasks[i].inbox);
    }
}

/* Lay a synthetic switched-out frame. switch_to pops ebx, esi, edi, ebp
 * then rets: the ret target is task_trampoline (sti), whose ret lands
 * in `entry` — the highest slot. */
void task_trampoline(void);   /* switch.S */

int task_spawn(void (*entry)(void)) {
    for (int i = 1; i < NTASK; i++) {
        if (tasks[i].state != ST_FREE) continue;
        u32 *sp = (u32 *)(kstacks[i] + STACK_BYTES);
        *--sp = (u32)entry;           /* trampoline's ret target */
        *--sp = (u32)task_trampoline; /* switch_to's ret target */
        *--sp = 0;                    /* ebp */
        *--sp = 0;                    /* edi */
        *--sp = 0;                    /* esi */
        *--sp = 0;                    /* ebx */
        tasks[i].esp = (u32)sp;
        tasks[i].state = ST_READY;
        return i;
    }
    return -1;
}

/* First READY task after `cur`; -1 if the current one is alone. */
static int pick_ready(void) {
    for (int k = 1; k < NTASK; k++) {
        int i = (cur + k) % NTASK;
        if (tasks[i].state == ST_READY) return i;
    }
    return -1;
}

static void sched_switch(int next) {
    int prev = cur;
    cur = next;
    switch_to(&tasks[prev].esp, tasks[next].esp);
}

void sched_tick(void) {
    if (cur < 0) return;
    int next = pick_ready();
    if (next >= 0) sched_switch(next);
}

/* Called when the current task blocks. If nobody is runnable the machine
 * is merely idle -- every server is asleep waiting for hardware -- so park
 * the CPU with interrupts on until an IRQ (the 100 Hz timer at the very
 * least) wakes somebody, then pick again. Two ways out: another task is
 * ready (switch), or the wake targeted us (return: the block is undone).
 * A true deadlock spins here forever, same as bigger kernels. */
void sched_next(void) {
    for (;;) {
        if (tasks[cur].state == ST_READY) return;
        int next = pick_ready();
        if (next >= 0) { sched_switch(next); return; }
        intr_enable();             /* hlt sleeps until the next interrupt */
        halt();
        intr_disable();
    }
}

void sched_wake(int tid) {
    if (tasks[tid].state == ST_BLOCKED) {
        tasks[tid].state = ST_READY;
        tasks[tid].woke = 1;
    }
}

/* kmain becomes "task 0": its saved esp is parked in tasks[0] forever. */
void sched_enter(int first) {
    cur = first;
    switch_to(&tasks[0].esp, tasks[first].esp);
    panic("sched_enter returned", 0);
}
