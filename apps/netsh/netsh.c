#include "../../kernel/string.h"
#include "../../kernel/types.h"
#include "../../kernel/syscall/syscall.h"
#include "../../servers/net/net.h"
#include "../../servers/tty/tty.h"

/* netsh: the net service's first customer. The stack above knows sockets
 * and nothing else; this task owns the policy -- what to listen on, what
 * to echo, and one greeting pushed outbound at boot. It is an event
 * loop: send a request, then answer replies as they land, in whatever
 * order the wire dictates. Several requests are in flight at once (a
 * parked accept and a parked recv for the udp echo, at the same time),
 * so the loop trusts the replies rather than guessing which one came
 * home: every open, accept, recv and send answer carries its own
 * socket id, and the open names the port it asked for. */

#define ECHO_PORT 2323                  /* tcp: echo back what it gets */
#define UDP_PORT  2325                  /* udp: same, datagram for datagram */
#define OUT_PORT  2324                  /* the host's ear, for the greeting */

static const u8 host_ip[4] = { 10, 0, 2, 2 };   /* the slirp gateway */
static const char greet[] = "hello from pomelo\n";

static int lis_sock = -1;               /* the tcp listener on 2323 */
static int cur_sock = -1;               /* the connection being echoed */
static int udp_sock = -1;               /* the udp echo socket on 2325 */
static int out_sock = -1;               /* the outbound greeting socket */

void netsh_main(void);
__attribute__((section(".hdr")))
const unsigned netsh_entry = (unsigned)netsh_main;

static void note(const char *s) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_PUTS;
    m.arg0 = strnlen(s, MSG_DATA);
    memcpy(m.data, s, m.arg0);
    sys_send_wait(TID_TTY, &m);
}

static void req_open(int proto, int lport, const u8 *dst, int dport) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_NET_OPEN;
    m.arg0 = proto;
    m.arg1 = lport;
    if (dst && dport) {                 /* all zero data = a listener */
        memcpy(m.data, dst, 4);
        m.data[4] = (u8)(dport >> 8);
        m.data[5] = (u8)dport;
    }
    sys_send_wait(TID_NET, &m);
}

static void req(int type, int sock, int arg1) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = type;
    m.arg0 = sock;
    m.arg1 = arg1;
    sys_send_wait(TID_NET, &m);
}

static void req_send(int sock, const char *s, int n) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_NET_SEND;
    m.arg0 = sock;
    m.arg1 = n;
    memcpy(m.data, s, n);
    sys_send_wait(TID_NET, &m);
}

static void arm_recv(int sock) {
    req(MSG_NET_RECV, sock, NET_CAP);
}

/* The arrival of one reply. The open knows which one it is by the local
 * port it asked for -- the only reply that carries such a hint. */
static void handle(struct msg *m) {
    switch (m->type) {
    case MSG_NET_REPLY_OPEN:
        if (m->arg0 < 0) {
            note("netsh: open refused\n");
            break;
        }
        if (m->arg1 == ECHO_PORT) {
            lis_sock = m->arg0;
            note("netsh: tcp echo on 2323\n");
            req(MSG_NET_ACCEPT, lis_sock, 0);
        } else if (m->arg1 == UDP_PORT) {
            udp_sock = m->arg0;
            note("netsh: udp echo on 2325\n");
            arm_recv(udp_sock);
        } else {                        /* neither echo port: the greeting */
            out_sock = m->arg0;
            req_send(out_sock, greet, (int)sizeof greet - 1);
        }
        break;

    case MSG_NET_REPLY_ACCEPT:
        if (m->arg0 < 0) {              /* the handshake died midway */
            note("netsh: accept failed\n");
            req_open(IP_PROTO_TCP, ECHO_PORT, 0, 0);
            break;
        }
        cur_sock = m->arg0;
        arm_recv(cur_sock);
        break;

    case MSG_NET_REPLY_RECV: {
        int sock = m->arg2;             /* whose recv this answers */
        if (m->arg1 < 0) {              /* the end: clean eof or a reset */
            note(m->arg0 == 0 ? "netsh: peer closed\n"
                              : "netsh: recv failed\n");
            if (sock == udp_sock) {
                udp_sock = -1;          /* udp has no goodbye; reopen */
                req_open(IP_PROTO_UDP, UDP_PORT, 0, 0);
                break;
            }
            if (m->arg0 == 0)           /* clean eof: our fin is still owed */
                req(MSG_NET_CLOSE, sock, 0);
            /* the listener became this connection -- one socket, one
             * call -- so a fresh listener takes the empty seat */
            cur_sock = -1;
            req_open(IP_PROTO_TCP, ECHO_PORT, 0, 0);
            break;
        }
        req_send(sock, m->data, m->arg0);        /* echo, then listen again */
        if (sock != udp_sock) cur_sock = sock;
        arm_recv(sock);
        break;
    }

    case MSG_NET_REPLY_SEND:
        if (m->arg0 < 0) {
            note("netsh: send refused\n");
            break;
        }
        if (m->arg2 == out_sock) {      /* the greeting is away: hang up */
            req(MSG_NET_CLOSE, out_sock, 0);
            out_sock = -1;
        }
        break;

    case MSG_NET_ERR:
        note("netsh: net says no: ");
        note((const char *)m->data);
        note("\n");
        break;
    }
}

void netsh_main(void) {
    note("netsh: up\n");
    req_open(IP_PROTO_TCP, 0, host_ip, OUT_PORT);   /* greeting outbound */
    req_open(IP_PROTO_TCP, ECHO_PORT, 0, 0);        /* the tcp listener */
    req_open(IP_PROTO_UDP, UDP_PORT, 0, 0);         /* the udp echo */

    struct msg m;
    for (;;) {
        sys_recv(&m);                   /* this task's whole world: replies */
        handle(&m);
    }
}
