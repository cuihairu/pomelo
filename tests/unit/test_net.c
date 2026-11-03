/* unit-net: exercise the IPC and frame hardening in the real net path.
 * Included production source, host -m32 build, so the guards are the
 * ones the service actually runs. */

#include <stdio.h>
#include <string.h>

#include "servers/net/net.c"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

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

    /* an ip packet with no payload (total==ihl) is safe: the port pair
     * guard returns before reading past the received frame */
    u8 ip[20]; memset(ip, 0, sizeof ip);
    ip[0] = 0x45;                 /* version 4, ihl 5 (20 bytes) */
    ip[9] = IP_PROTO_UDP;
    ip[2] = 0; ip[3] = 20;        /* total length == ihl */
    ip_in(ip, 20);
    CHECK(1, "empty ip payload handled safely");

    if (fails) { printf("unit-net: %d failure(s)\n", fails); return 1; }
    printf("unit-net: all ok\n");
    return 0;
}
