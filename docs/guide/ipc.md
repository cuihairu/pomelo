# 05 · IPC

Corresponding code: `kernel/ipc/`

## Between services, one wall and one door

In a microkernel, two services share no variables and call no functions of each other; their only way of contact is **messages**. Pomelo's message is fixed-length, simple enough:

```c
struct msg {
    int src;              /* who sent it, filled in by the kernel */
    int type;             /* request type: FS_READ / TTY_GETLINE ... */
    int arg0, arg1, arg2; /* three arguments, their meaning fixed by type */
    char data[MSG_DATA];  /* 56 bytes of accompanying data, like a file name */
};

struct msgq {
    struct msg buf[MSGQ_CAP];  /* 16 entries: a fixed-length queue, enough for one round of shell output */
    int head, count;
};
```

Fixed-length messages save you memory allocation and lifetime management — a big simplification. The price is that moving a big chunk of data has to be done **in several trips** — for example, when fs reads a file it first replies with the length, and then the shell fetches it batch by batch. For self-study this trade-off is well worth it.

## One queue per task

The message queue hangs on the task itself (`struct task.inbox`), and the kernel only provides delivery and pickup:

```c
static int enqueue(int dst, struct msg *m, int src) {
    if (dst < 1 || dst >= NTASK) return -1;
    intr_disable();                       /* enqueue+wake kept atomic */
    struct msgq *q = &tasks[dst].inbox;
    if (q->count == MSGQ_CAP) {
        intr_enable();
        return -1;                        /* queue full, return failure first */
    }
    *slot(q, q->count) = *m;              /* slot: the i-th slot counting from head */
    slot(q, q->count)->src = src;
    q->count++;
    intr_enable();
    sched_wake(dst);                      /* the recipient may be waiting; wake it */
    return 0;
}

int ipc_send(int dst, struct msg *m) {
    return enqueue(dst, m, cur);          /* a task sends a letter, src = the sender */
}

int ipc_recv(struct msg *out, int block) {
    for (;;) {
        intr_disable();
        struct msgq *q = &tasks[cur].inbox;
        if (q->count) {
            *out = *slot(q, 0);           /* always take from the head of the queue */
            q->head = (q->head + 1) % MSGQ_CAP;
            q->count--;
            intr_enable();
            return 0;
        }
        if (!block) {
            intr_enable();
            return -1;
        }
        tasks[cur].state = ST_BLOCKED;    /* sleep until someone sends a message */
        sched_next();
    }
}
```

The two pieces each have their subtleties: `enqueue` keeps "enqueue+wake" entirely inside an interrupt-disabled critical section — cut off by the clock halfway through delivery and the message is left half-done; the **looping check** of `ipc_recv` is the classic trap of the concurrency lesson: being woken does not mean there is definitely a message, and writing it as an `if` will occasionally hand you an empty answer. The kernel itself also has to send letters (the characters interrupts toss to the tty, mouse samples) and goes through `ipc_send_kernel` — the same `enqueue`, signed `src=0`, "this letter was written by no task".

## One shell file read: how the messages travel back and forth

The best way to make IPC clear is to walk through a real timing. When shell runs `cat hello.txt`:

```
shell                       kernel/ipc                    fs task
  │  SEND{FS_OPEN, "hello.txt"} │                             │
  ├───────────────────────────▶ │  enqueue, wake fs           │
  │  RECV ──▶ blocks           │ ─────────────────────────▶ │ receives
  │                             │                             │ looks up the inode
  │        ◀── RECV{FS_OPEN, size=26} ◀────────────────────────┤
  │  SEND{FS_READ, 0}           │                             │
  ├───────────────────────────▶ │ ─────────────────────────▶ │ reads the data block
  │        ◀── RECV{FS_READ, data...} ◀────────────────────────┤
  │  print to the tty           │
```

Across the whole chain, shell has no idea what the disk looks like, and fs has no idea who is asking it. Swap out fs's implementation and shell does not change a single line — this is the **replaceability** you get for cutting services out of the kernel.

## Deadlock: two tasks waiting on each other

The fixed-length queue has limited capacity. If shell sends A to fs and fs in turn sends B to shell, and both sides are full, they end up waiting on each other. Pomelo's approach is crude but never wrong: **only shell makes one-way requests; services never request from each other**. Real systems either use timeouts or an acyclic call graph — left to you as an exercise.

## The sender: a request never delivered is a reply never coming

`ipc_send` returns -1 when the queue is full; it does not wait. No one can ignore that -1: the kernel-side interrupt path answers with backpressure — if the queue cannot hold it, do not read from the FIFO ([the terminal service chapter](/guide/tty-server)); the rule on the task side is **retry**:

```c
static inline void sys_send_wait(int dst, struct msg *m) {
    while (sys_send(dst, m) < 0) sys_yield();
}
```

When full, give up the time slice and try again after the receiver frees a slot. It does not degenerate into deadlock, and it leans on one discipline of the receiver: the main loop sleeps only when the queue is **empty**, and never waits when the queue is **full** — the queue will always free up. Every output of shell, every fs request, every line the tty tosses to shell, every reply fs sends back goes through this same version. This rule was once bought with a real accident: ls's output crowded the tty's queue full, shell's `getline` request was silently dropped, and from then on shell waited for a line that would never come.

## Files in this chapter

```
kernel/ipc/ipc.h     the msg struct and the queue API
kernel/ipc/ipc.c     the fixed-length ring queue, send/recv, block/wake
```

The next chapter: [The syscall gate](/guide/syscall).
