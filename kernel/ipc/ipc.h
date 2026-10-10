#ifndef POMELO_IPC_H
#define POMELO_IPC_H

#include "../types.h"

#define MSG_DATA 56      /* payload bytes carried per message */
#define MSGQ_CAP 16      /* deep enough for one shell output burst */

/* Fixed-size messages: no allocation, no lifetime questions. The `type`
 * field selects the protocol (see servers/fs/fs.h, servers/tty/tty.h);
 * args and data mean whatever the two endpoints agree on. */
struct msg {
    int src;             /* filled in by the kernel on send */
    int type;
    int arg0, arg1, arg2;
    char data[MSG_DATA];
};

struct msgq {
    struct msg buf[MSGQ_CAP];
    int head, count;
};

void msgq_reset(struct msgq *q);
int  ipc_send(int dst, struct msg *m);       /* 0 ok, -1 queue full */
int  ipc_send_kernel(int dst, struct msg *m); /* same, signed by the kernel */
int  ipc_can_send(int dst);                  /* room for one more message? */
int  ipc_recv(struct msg *out, int block);   /* 0 ok, -1 if !block and empty */

#endif
