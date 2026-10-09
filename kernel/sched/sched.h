#ifndef POMELO_SCHED_H
#define POMELO_SCHED_H

#include "../types.h"
#include "../ipc/ipc.h"

#define NTASK 8
#define STACK_BYTES 4096

enum { ST_FREE, ST_READY, ST_BLOCKED };

struct task {
    u32 esp;            /* saved kernel sp while not running */
    int state;
    int woke;           /* set by sched_wake; checked by irq_wait */
    struct msgq inbox;  /* this task's messages, see kernel/ipc */
};

extern struct task tasks[NTASK];
extern int cur;         /* running task index, -1 while in kmain */

void sched_init(void);
int  task_spawn(void (*entry)(void));  /* returns tid or -1 */
void sched_enter(int first);           /* kmain jumps into `first` */
void sched_tick(void);                 /* timer preemption point */
void sched_next(void);                 /* current blocks: pick another */
void sched_wake(int tid);

#endif
