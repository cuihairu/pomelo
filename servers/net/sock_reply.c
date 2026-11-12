#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "proto.h"

/* The one reply the socket layer sends outside a request/response pair:
 * the peer is gone, so a reader hears the end. Kept in its own
 * translation unit so the host unit test can supply its own
 * sock_eof_reply (capturing who the reply went to) without the service
 * ever compiling a test branch -- the service links this file, the
 * test does not. */

void sock_eof_reply(int dst, int sid) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_REPLY_RECV;
    r.arg0 = 0;
    r.arg1 = -1;
    r.arg2 = sid;
    sys_send_wait(dst, &r);
}
