/* unit-irq-release: the net irq stub residue the November audit demanded.
 * net installs its stub and claims a line through sys_irq_enable; when it
 * dies, that claim survives -- the stub stays in handlers[], the claimed
 * bit keeps the kernel from ever eoi-ing the line, and the line sits
 * in-service forever, strangling the next driver that wants it. irq_release
 * is the teardown; this test drives the real irq.c and checks the lines
 * it gives back.
 *
 * Only the seat that claimed a line may release it: a line owned by a
 * live driver is not touched by another task's death. And the claim
 * itself is exclusive: the kernel's own lines are never handed out, and
 * a second seat cannot take a line the first still holds. */

#include <stdio.h>

#include "kernel/intr/intr.h"
#include "kernel/sched/sched.h"

/* The pic calls irq_release and a refused claim would make. The test
 * supplies its own (no intr.c, no port I/O) so the actions are recorded. */
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
    /* the kernel's own lines are not a driver's to take: the clock, the
     * keyboard, the cascade, the serial port, the mouse. irq_claim is
     * where sys_irq_enable's refusal comes from. */
    cur = 7;
    CHECK(irq_claim(IRQ_PIT) == -1, "the clock line is refused");
    CHECK(irq_claim(IRQ_KBD) == -1, "the keyboard line is refused");
    CHECK(irq_claim(2) == -1, "the cascade line is refused");
    CHECK(irq_claim(IRQ_COM1) == -1, "the serial line is refused");
    CHECK(irq_claim(IRQ_MOUSE) == -1, "the mouse line is refused");
    CHECK(irq_owned(IRQ_PIT) == 0, "a refused claim leaves nothing owned");

    /* task 7 claims line 5, task 8 claims line 6 */
    cur = 7;
    CHECK(irq_claim(5) == 0, "line 5 is task 7's to claim");
    irq_install(5, count_handler);
    cur = 8;
    CHECK(irq_claim(6) == 0, "line 6 is task 8's to claim");
    CHECK(irq_owned(5) == 1, "line 5 is claimed by task 7");
    CHECK(irq_owned(6) == 1, "line 6 is claimed by task 8");

    /* a claim is exclusive: task 8 cannot take line 5 over task 7 */
    CHECK(irq_claim(5) == -1, "a second seat cannot steal line 5");
    CHECK(irq_owned_by(5, 7) == 1, "line 5 still belongs to task 7");
    CHECK(irq_owned_by(5, 8) == 0, "and not to task 8");
    CHECK(irq_owned_by(6, 8) == 1, "line 6 belongs to task 8");
    CHECK(irq_owned_by(3, 7) == 0, "an unclaimed line belongs to nobody");

    /* SYS_V2P asks which line a seat drives, so it knows whether the
     * frames it converts are DMA targets at all */
    cur = 7;
    CHECK(irq_line_of(7) == 5, "task 7 answers with the line it claimed");
    CHECK(irq_line_of(8) == 6, "task 8 answers with its own");
    CHECK(irq_line_of(3) == -1, "a seat that claims nothing has no line");

    /* the owner re-claiming its own line is a no-op, so a driver that
     * re-runs its bringup stays put */
    cur = 7;
    CHECK(irq_claim(5) == 0, "the owner may re-claim its own line");
    CHECK(irq_owned_by(5, 7) == 1, "and keeps it");

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

    /* a released line is claimable again -- exclusivity ends with its
     * owner, it is not a life sentence for the hardware */
    cur = 8;
    CHECK(irq_claim(5) == 0, "line 5 is free for the next driver");
    CHECK(irq_owned_by(5, 8) == 1, "now it belongs to task 8");
    irq_release(8);                       /* give both lines back */
    CHECK(irq_owned(5) == 0 && irq_owned(6) == 0, "task 8's lines released");

    /* releasing an already-free line is a no-op, not a crash */
    disn = eoin = 0;
    irq_release(7);
    CHECK(disn == 0 && eoin == 0, "releasing a free line does nothing");

    if (fails) { printf("unit-irq-release: %d failure(s)\n", fails); return 1; }
    printf("unit-irq-release: all ok\n");
    return 0;
}
