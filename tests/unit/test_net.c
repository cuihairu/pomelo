/* unit-net: exercise the IPC and frame hardening in the real net path.
 * The service's modules are compiled in as-is (nic/arp/ip/sock/net),
 * host -m32 build, so the guards under test are the ones the service
 * actually runs. Only the boundary is inert: no card is probed and no
 * syscall is entered, so the host run touches no hardware. */

#include <stdio.h>
#include <string.h>

#include "servers/net/proto.h"
#include "kernel/syscall/syscall.h"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

/* The service's replies leave through sock_eof_reply, which the real
 * service links from sock_reply.c. The test does not link that file;
 * it supplies its own, so sock.c's reference resolves here and the
 * test can see which task a recv-after-fin was answered. */
static int wrap_dst, wrap_sid, wrap_calls;
void sock_eof_reply(int dst, int sid) {
    wrap_dst = dst; wrap_sid = sid; wrap_calls++;
}

int main(void) {
    /* TCP/UDP SEND length is clamped to what one message can carry */
    CHECK(net_clamp_send_len(-1) == 0, "negative send length clamps to 0");
    CHECK(net_clamp_send_len(0) == 0, "zero send length stays 0");
    CHECK(net_clamp_send_len(MSG_DATA) == MSG_DATA,
          "send length at MSG_DATA kept");
    CHECK(net_clamp_send_len(MSG_DATA + 1) == MSG_DATA,
          "send length above MSG_DATA clamped");

    /* a runt arp frame is dropped at the ethernet gate */
    u8 f[41]; memset(f, 0, sizeof f);
    f[12] = ETH_ARP >> 8; f[13] = ETH_ARP & 0xff;
    arp_ready = 0;
    net_rx(f, 41);
    CHECK(arp_ready == 0, "runt arp frame dropped");

    /* an ip packet with no payload (total==ihl) is rejected before the
     * UDP branch reads a port pair past the received frame */
    u8 ip[20]; memset(ip, 0, sizeof ip);
    ip[0] = 0x45;                 /* version 4, ihl 5 (20 bytes) */
    ip[9] = IP_PROTO_UDP;
    ip[2] = 0; ip[3] = 20;        /* total length == ihl */
    ip[16] = 10; ip[17] = 0; ip[18] = 2; ip[19] = 15;
    u16 sum = cksum(ip, 20);
    ip[10] = (u8)(sum >> 8); ip[11] = (u8)sum;
    CHECK(ip_in(ip, 20) == 0, "empty ip payload rejected");

    /* a recv that finds the peer already gone (fin seen, buffer empty)
     * must answer the caller who just asked -- not a stale seat from a
     * previous request that never cleared its pending src. The wrapped
     * sock_eof_reply shows exactly who the reply went to. */
    struct sock *sf = sock_alloc();
    sf->fin_seen = 1;
    sf->pending.src = 42;              /* a stale src: must not hear it */
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_NET_RECV;
    m.arg0 = 0;                        /* sock_alloc returns socks[0] fresh */
    m.arg1 = 100;
    m.src = 7;                         /* the current caller */
    wrap_dst = -999; wrap_sid = -999; wrap_calls = 0;
    sock_api(&m);
    CHECK(wrap_calls == 1, "recv-after-fin answers once");
    CHECK(wrap_dst == 7, "recv-after-fin answers the current caller");
    CHECK(wrap_sid == 0, "recv-after-fin names the socket");

    if (fails) { printf("unit-net: %d failure(s)\n", fails); return 1; }
    printf("unit-net: all ok\n");
    return 0;
}
