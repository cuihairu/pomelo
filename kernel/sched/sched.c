#include "sched.h"
#include "../intr/intr.h"
#include "../mm/paging.h"
#include "../../boot/gdt.h"

struct task tasks[NTASK];
int cur = -1;

static u8 kstacks[NTASK][STACK_BYTES] __attribute__((aligned(16)));

void switch_to(u32 *save_esp, u32 new_esp);   /* switch.S */

void sched_init(void) {
    for (int i = 0; i < NTASK; i++) {
        tasks[i].state = ST_FREE;
        tasks[i].woke = 0;
        tasks[i].pdir = 0;
        msgq_reset(&tasks[i].inbox);
    }
}

/* A task is born in ring 3: switch_to rets into a trampoline, but this
 * one irets -- the stack below it is a complete user frame (ss, esp,
 * eflags, cs, eip, top down), so the CPU drops to ring 3 on the way in
 * and can only come back through the syscall gate. There is no ring-0
 * spawn: every service the kernel starts lives in its own address
 * space, entered exactly like a user program. */
void ring3_entry(void);       /* switch.S */

int task_spawn_user(u32 dir, u32 entry, u32 ustack_top, u32 flags) {
    for (int i = 1; i < NTASK; i++) {
        if (tasks[i].state != ST_FREE) continue;
        u32 *sp = (u32 *)(kstacks[i] + STACK_BYTES);
        *--sp = USER_DS;              /* ss: the frame iret walks bottom-up */
        *--sp = ustack_top;           /* user esp */
        *--sp = flags;                /* eflags: EFLAGS_USER, maybe + IOPL */
        *--sp = USER_CS;              /* user cs */
        *--sp = entry;                /* user eip */
        *--sp = (u32)ring3_entry;     /* switch_to's ret target */
        *--sp = 0;                    /* ebp */
        *--sp = 0;                    /* edi */
        *--sp = 0;                    /* esi */
        *--sp = 0;                    /* ebx */
        tasks[i].esp = (u32)sp;
        tasks[i].pdir = dir;
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

/* The world switches with the task: cr3 selects whose pages are real,
 * and esp0 points the CPU at this task's kernel stack for when ring 3
 * next knocks on a gate. */
static void sched_switch(int next) {
    int prev = cur;
    cur = next;
    tss_esp0_set((u32)(kstacks[next] + STACK_BYTES));
    load_cr3(tasks[next].pdir ? tasks[next].pdir : (u32)kernel_pdir);
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
    tss_esp0_set((u32)(kstacks[first] + STACK_BYTES));
    load_cr3(tasks[first].pdir ? tasks[first].pdir : (u32)kernel_pdir);
    switch_to(&tasks[0].esp, tasks[first].esp);
    panic("sched_enter returned", 0);
}
