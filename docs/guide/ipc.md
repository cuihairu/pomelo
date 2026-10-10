# 05 · IPC

对应代码:`kernel/ipc/`

## 服务之间的墙,只有一扇门

微内核里,两个服务不共享变量、不互相调用函数,它们唯一的联系方式是**消息**。
Pomelo 的消息是定长的,足够简单:

```c
struct msg {
    int src;              /* 谁发的,内核代填 */
    int type;             /* 请求类型:FS_READ / TTY_GETLINE ... */
    int arg0, arg1, arg2; /* 三个参数,具体含义由 type 约定 */
    char data[56];        /* 一小段随行数据,如文件名 */
};
```

定长消息省掉了内存分配和生命周期管理,是一大简化。代价是传大块数据得**分多次**——
比如 fs 读一个文件,先回长度,再由 shell 分批取。教学中这个取舍很划算。

## 每个任务一个队列

消息队列挂在任务自己身上(`struct task.inbox`),内核只提供投递和领取:

```c
int ipc_send(int dst, struct msg *m) {
    if (dst < 1 || dst >= NTASK) return -1;
    intr_disable();                       /* 入队+唤醒保持原子 */
    struct msgq *q = &tasks[dst].inbox;
    if (q->count == MSGQ_CAP) {
        intr_enable();
        return -1;                        /* 队列满,先返回失败 */
    }
    *slot(q, q->count) = *m;              /* slot:head 起第 i 个槽位 */
    slot(q, q->count)->src = cur;
    q->count++;
    intr_enable();
    sched_wake(dst);                      /* 收件人可能正等着,叫醒它 */
    return 0;
}

int ipc_recv(struct msg *out, int block) {
    for (;;) {
        intr_disable();
        struct msgq *q = &tasks[cur].inbox;
        if (q->count) {
            *out = *slot(q, 0);           /* 永远从队头取 */
            q->head = (q->head + 1) % MSGQ_CAP;
            q->count--;
            intr_enable();
            return 0;
        }
        if (!block) { intr_enable(); return -1; }
        tasks[cur].state = ST_BLOCKED;    /* 睡,直到有人发消息 */
        sched_next();
    }
}
```

两段各有讲究:`ipc_send` 把“入队+唤醒”整个关在关中断的临界区里——投递到一半
被时钟打断,消息会处于半截状态;`ipc_recv` 的**循环检查**则是并发课的经典陷阱:
被唤醒不代表一定有消息,写成 `if` 会偶发地拿到空答案。

## 一次 shell 读文件,消息如何往返

把 IPC 讲清楚的最好办法是走一遍真实时序。shell 执行 `cat hello.txt` 时:

```
shell                       kernel/ipc                    fs 任务
  │  SEND{FS_OPEN, "hello.txt"} │                             │
  ├───────────────────────────▶ │  入队,唤醒 fs               │
  │  RECV ──▶ 阻塞              │ ─────────────────────────▶ │ 收到
  │                             │                             │ 查 inode
  │        ◀── RECV{FS_OPEN, size=26} ◀────────────────────────┤
  │  SEND{FS_READ, 0}           │                             │
  ├───────────────────────────▶ │ ─────────────────────────▶ │ 读数据块
  │        ◀── RECV{FS_READ, data...} ◀────────────────────────┤
  │  打印到 tty                 │
```

整条链上,shell 完全不知道磁盘长什么样,fs 也完全不知道谁在问它。换掉 fs 的实现,
shell 一行都不用改——这就是把服务切出内核换来的**可替换性**。

## 死锁:两个任务互等

定长队列的容量有限。如果 shell 给 fs 发了 A、fs 又反过来给 shell 发 B,而两边
都塞满了,就互等了。Pomelo 的做法很土但从不出错:**只允许 shell 单向请求,
服务之间不互相请求**。真实系统里要么用超时,要么用无环的调用图——留给你当练习。

## 发送方:没递出去的请求,是永远等不到的回复

`ipc_send` 在队列满时返回 -1,不等待。这个 -1 谁都不能无视:内核侧的中断路径
用背压应对——队列装不下就不从 FIFO 里读([终端服务章](/guide/tty-server));
任务侧的规矩则是**重试**:

```c
static inline void sys_send_wait(int dst, struct msg *m) {
    while (sys_send(dst, m) < 0) sys_yield();
}
```

满了就让出时间片,等接收方腾出位置再试。它不会退化成死锁,靠的是接收方的
一个纪律:主循环只在队列**空**时睡,从不在队列**满**时等——队列总会腾出来。
shell 的每一次输出、每一条 fs 请求,tty 投给 shell 的每一行,fs 回的每一个
应答,走的都是这个版本。这条规矩曾用一次真实事故换来:ls 的输出把 tty 的
队列挤满,shell 的 `getline` 请求被静默丢弃,shell 从此等一条永远不会来的行。

## 本章文件

```
kernel/ipc/ipc.h     msg 结构与队列 API
kernel/ipc/ipc.c     定长环形队列、send/recv、阻塞唤醒
```

下一章:[系统调用门](/guide/syscall)。
