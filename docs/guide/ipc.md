# 05 · IPC

对应代码:`kernel/ipc/`

## 服务之间的墙,只有一扇门

微内核里,两个服务不共享变量、不互相调用函数,它们唯一的联系方式是**消息**。
Pomelo 的消息是定长的,足够简单:

```c
struct msg {
    int src;              /* 谁发的,内核代填 */
    int type;             /* 请求类型:FS_READ / TTY_GETLINE ... */
    int arg0, arg1;       /* 两个参数,具体含义由 type 约定 */
    char data[16];        /* 一小段随行数据,如文件名 */
};
```

定长消息省掉了内存分配和生命周期管理,是一大简化。代价是传大块数据得**分多次**——
比如 fs 读一个文件,先回长度,再由 shell 分批取。教学中这个取舍很划算。

## 每个任务一个队列

消息队列挂在任务自己身上(`struct task.inbox`),内核只提供投递和领取:

```c
int ipc_send(int dst, struct msg *m) {
    if (ipc_enqueue(dst, m) < 0) return -1;   /* 队列满,先返回失败 */
    if (tasks[dst].state == BLOCKED)
        sched_wake(dst);                      /* 收件人可能正等着,叫醒它 */
    return 0;
}

int ipc_recv(struct msg *out) {
    while (ipc_empty(cur))                    /* 没消息就睡,直到被唤醒 */
        sched_block();
    ipc_dequeue(cur, out);
    return 0;
}
```

注意 `ipc_recv` 的循环:被唤醒不代表一定有消息(可能是别的原因醒的),所以要
**循环检查**。写成 `if` 会偶发地拿到空消息——这是并发代码的经典陷阱。

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

## 本章文件

```
kernel/ipc/ipc.h     msg 结构与队列 API
kernel/ipc/ipc.c     定长环形队列、send/recv、阻塞唤醒
```

下一章:[系统调用门](/guide/syscall)。
