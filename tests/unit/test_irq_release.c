/* unit-irq-release: the net irq stub residue the November audit demanded.
 * net installs its stub and claims a line through sys_irq_enable; when it
 * dies, that claim survives -- the stub stays in handlers[], the claimed
 * bit keeps the kernel from ever eoi-ing the line, and the line sits
 * in-service forever, strangling the next driver that wants it. irq_release
 * is the teardown; this test drives the real irq.c and checks the lines
 * it gives back.
 *
 * Only the seat that claimed a line may release it: a line owned by a
 * live driver is not touched by another task's death. */

#include <stdio.h>

#include "kernel/intr/intr.h"
#include "kernel/sched/sched.h"

/* The pic calls irq_release makes on the way out. The test supplies its
 * own (no intr.c, no port I/O) so the release's actions are recorded. */
static u32 dis[16], disn;
static u32 eoi[16], eoin;
void pic_irq_disable(u32 irq)   { if (disn < 16) dis[disn++] = irq; }
void pic_eoi_specific(u32 irq)  { if (eoin < 16) eoi[eoin++] = irq; }

/* irq.c also wakes waiters and reads the task table. */
void sched_wake(int tid) { (void)tid; }
void sched_next(void) {}
struct task tasks[NTASK];
int cur = -1;

static int count_calls;
static void count_handler(void) { count_calls++; }

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* task 7 claims line 5, task 8 claims line 6 */
    cur = 7;
    irq_install(5, count_handler);
    irq_claim(5);
    cur = 8;
    irq_claim(6);
    CHECK(irq_owned(5) == 1, "line 5 is claimed by task 7");
    CHECK(irq_owned(6) == 1, "line 6 is claimed by task 8");

    /* task 7 dies: line 5 goes back, line 6 (owned by 8) stays */
    disn = eoin = 0;
    irq_release(7);
    CHECK(irq_owned(5) == 0, "task 7's death releases line 5");
    CHECK(irq_owned(6) == 1, "another seat's line is not touched");
    CHECK(disn == 1 && dis[0] == 5, "line 5 is re-masked on release");
    CHECK(eoin == 1 && eoi[0] == 5, "line 5 is acked on release");

    /* the stub is gone: dispatching the line no longer runs a handler */
    count_calls = 0;
    irq_dispatch(5);
    CHECK(count_calls == 0, "the dead driver's stub is removed");

    /* task 8 dies: line 6 goes back too */
    disn = eoin = 0;
    irq_release(8);
    CHECK(irq_owned(6) == 0, "task 8's death releases line 6");
    CHECK(disn == 1 && dis[0] == 6, "line 6 is re-masked on release");

    /* releasing an already-free line is a no-op, not a crash */
    disn = eoin = 0;
    irq_release(7);
    CHECK(disn == 0 && eoin == 0, "releasing a free line does nothing");

    if (fails) { printf("unit-irq-release: %d failure(s)\n", fails); return 1; }
    printf("unit-irq-release: all ok\n");
    return 0;
}
