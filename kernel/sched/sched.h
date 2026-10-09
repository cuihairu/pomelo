#ifndef POMELO_SCHED_H
#define POMELO_SCHED_H

#include "../types.h"
#include "../ipc/ipc.h"

#define NTASK 8
#define STACK_BYTES 4096

enum { ST_FREE, ST_READY, ST_BLOCKED };

struct task {
    u32 esp;            /* saved kernel sp while not running */
    u32 pdir;           /* page directory, 0 = the kernel's own */
    int state;
    int woke;           /* set by sched_wake; checked by irq_wait */
    struct msgq inbox;  /* this task's messages, see kernel/ipc */
};

extern struct task tasks[NTASK];
extern int cur;         /* running task index, -1 while in kmain */

/* User eflags at spawn. Plain tasks get interrupts on; the fs service
 * also gets IOPL=3 because its ATA driver does `in`/`out` (and cli/sti
 * pairs around its waits) from ring 3 -- the x86 only lets a ring-3
 * task touch ports when IOPL >= CPL. */
#define EFLAGS_USER      0x202    /* interrupts on, reserved bit set */
#define EFLAGS_USER_IOPL 0x3202   /* same + IOPL=3: may do port I/O */

void sched_init(void);
int  task_spawn_user(u32 dir, u32 entry, u32 ustack_top, u32 flags);
void sched_enter(int first);           /* kmain jumps into `first` */
void sched_tick(void);                 /* timer preemption point */
void sched_next(void);                 /* current blocks: pick another */
void sched_wake(int tid);

#endif
