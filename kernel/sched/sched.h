#ifndef POMELO_SCHED_H
#define POMELO_SCHED_H

#include "../types.h"
#include "../ipc/ipc.h"

#define NTASK 8
#define STACK_BYTES 4096

/* User eflags at spawn. Plain tasks get interrupts on; the fs service
 * also gets IOPL=3 because its ATA driver does `in`/`out` (and cli/sti
 * pairs around its waits) from ring 3 -- the x86 only lets a ring-3
 * task touch ports when IOPL >= CPL. */
#define EFLAGS_USER      0x202    /* interrupts on, reserved bit set */
#define EFLAGS_USER_IOPL 0x3202   /* same + IOPL=3: may do port I/O */

enum { ST_FREE, ST_READY, ST_BLOCKED };

/* Programs the kernel image carries, registered by boot/main.c. spawn
 * can only start these, by name: a task asks with a word, never with
 * an address, so it can never make the kernel map arbitrary memory.
 * The registry is the capability list. */
#define PROG_NAME 8
#define NPROG     4
struct prog {
    char      name[PROG_NAME];
    const u8 *begin, *end;   /* the blob, inside the kernel image */
    u32       flags;         /* birth eflags: EFLAGS_USER, maybe IOPL */
};

struct ps_entry {            /* what SYS_PS hands a task, one per seat */
    int  tid;
    int  state;
    char name[PROG_NAME];
};

struct task {
    u32 esp;            /* saved kernel sp while not running */
    u32 pdir;           /* page directory, 0 = the kernel's own */
    int state;
    int woke;           /* set by sched_wake; checked by irq_wait */
    const struct prog *prog;  /* which program this seat belongs to */
    struct msgq inbox;  /* this task's messages, see kernel/ipc */
};

extern struct task tasks[NTASK];
extern int cur;         /* running task index, -1 while in kmain */

void sched_init(void);
int  task_start(int slot, u32 dir, u32 entry, u32 ustack_top, u32 flags);
void sched_enter(int first);           /* kmain jumps into `first` */
void sched_tick(void);                 /* timer preemption point */
void sched_next(void);                 /* current blocks: pick another */
void sched_wake(int tid);

/* Programs: registration and the one door every start goes through. */
int  prog_add(const char *name, const u8 *begin, const u8 *end, u32 flags);
int  sched_spawn(const char *name);              /* tid, or -1 */
int  ps_snapshot(struct ps_entry *out, int max); /* rows for SYS_PS */

#endif
