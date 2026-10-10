#include "ipc.h"
#include "../sched/sched.h"

/* Ring buffer of fixed-size messages. Index i counts from head. */
static struct msg *slot(struct msgq *q, int i) {
    return &q->buf[(q->head + i) % MSGQ_CAP];
}

void msgq_reset(struct msgq *q) {
    q->head = q->count = 0;
}

static int enqueue(int dst, struct msg *m, int src) {
    if (dst < 1 || dst >= NTASK) return -1;
    intr_disable();                       /* enqueue+wake stay atomic */
    struct msgq *q = &tasks[dst].inbox;
    if (q->count == MSGQ_CAP) {
        intr_enable();
        return -1;                        /* caller retries or fails */
    }
    *slot(q, q->count) = *m;
    slot(q, q->count)->src = src;
    q->count++;
    intr_enable();
    sched_wake(dst);                      /* maybe the receiver sleeps */
    return 0;
}

int ipc_send(int dst, struct msg *m) {
    return enqueue(dst, m, cur);
}

/* Kernel-to-task mail: the interrupt path and syscalls deliver events
 * this way, signed "0" because no task sent them. */
int ipc_send_kernel(int dst, struct msg *m) {
    return enqueue(dst, m, 0);
}

/* Room in dst's inbox? Interrupt handlers ask before they read: a byte
 * left in the UART FIFO is not lost, one taken out and dropped is. */
int ipc_can_send(int dst) {
    return tasks[dst].inbox.count < MSGQ_CAP;
}

int ipc_recv(struct msg *out, int block) {
    for (;;) {
        intr_disable();
        struct msgq *q = &tasks[cur].inbox;
        if (q->count) {
            *out = *slot(q, 0);
            q->head = (q->head + 1) % MSGQ_CAP;
            q->count--;
            intr_enable();
            return 0;
        }
        if (!block) {
            intr_enable();
            return -1;
        }
        tasks[cur].state = ST_BLOCKED;    /* sleep until a send wakes us */
        sched_next();
    }
}
