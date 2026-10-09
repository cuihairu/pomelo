#include "sched.h"
#include "../mm/paging.h"
#include "../string.h"

/* The program registry, and the one door every start goes through.
 * Boot is just the first spawner: it registers the four blobs and calls
 * the same sched_spawn a shell command later would -- restarting a
 * service is the same operation as starting it, not a new mechanism. */

static struct prog progs[NPROG];
static int nprogs;

int prog_add(const char *name, const u8 *begin, const u8 *end, u32 flags) {
    if (nprogs == NPROG) return -1;
    struct prog *p = &progs[nprogs++];
    int i = 0;
    for (; name[i] && i < PROG_NAME - 1; i++) p->name[i] = name[i];
    p->name[i] = 0;
    p->begin = begin;
    p->end = end;
    p->flags = flags;
    return 0;
}

/* Start a program by name. A dead service gets its old seat back, so
 * its task id -- and every message addressed to it -- survives the
 * crash: that is what makes "restart the server" a complete recovery
 * story instead of a reboot. */
int sched_spawn(const char *name) {
    struct prog *p = 0;
    for (int i = 0; i < nprogs; i++)
        if (!strncmp(progs[i].name, name, PROG_NAME)) p = &progs[i];
    if (!p) return -1;

    int slot = -1;
    for (int i = 1; i < NTASK; i++)     /* the old seat first */
        if (tasks[i].prog == p && tasks[i].state == ST_FREE) { slot = i; break; }
    if (slot < 0)
        for (int i = 1; i < NTASK; i++)
            if (tasks[i].state == ST_FREE) { slot = i; break; }
    if (slot < 0) return -1;            /* machine is full */

    u32 dir = pdir_user_new((u32)p->begin, (u32)(p->end - p->begin));
    if (!dir) return -1;                /* out of frames */
    if (task_start(slot, dir, *(const u32 *)p->begin, USER_STACK_TOP, p->flags) < 0)
        return -1;
    tasks[slot].prog = p;
    return slot;
}

/* SYS_PS: a read-only peek at the task table, one row per seat. */
int ps_snapshot(struct ps_entry *out, int max) {
    int n = 0;
    for (int i = 1; i < NTASK && n < max; i++) {
        out[n].tid = i;
        out[n].state = tasks[i].state;
        int j = 0;
        if (tasks[i].prog)
            for (; tasks[i].prog->name[j] && j < PROG_NAME - 1; j++)
                out[n].name[j] = tasks[i].prog->name[j];
        out[n].name[j] = 0;
        n++;
    }
    return n;
}
