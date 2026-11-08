#include "../../kernel/string.h"
#include "proto.h"

/* The datagram layer and icmp. Outbound is one shape: wrap a payload
 * in a 20-byte header and the cached arp mac, addressed to the gateway.
 * Inbound checks the envelope, then hands the payload to icmp or to
 * whichever socket owns the port. Fragmentation, options and tos are
 * all left out -- the frames this stack sends fit one descriptor, and
 * one peer needs no routing decision. */

/* The one's-complement checksum over n bytes, per rfc 1071. An odd
 * trailing byte is zero-padded, so a stale byte past a runt segment
 * can never leak into the sum. */
u16 cksum(const u8 *b, int n) {
    u32 s = 0;
    for (int i = 0; i < n; i += 2) {
        u32 w = (u32)b[i] << 8;
        if (i + 1 < n) w |= b[i + 1];
        s += w;
    }
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return (u16)(~s & 0xFFFF);
}

static u16 ip_id;

/* Send one ip datagram to the gateway (stage 2's only peer). */
void ip_tx(const u8 dst[4], u8 proto, const u8 *payload, int len) {
    u8 f[RX_BUF];
    if (!arp_ready || memcmp(dst, ip_gw, 4))
        return;
    memcpy(f, arp_mac, 6);
    memcpy(f + 6, mac, 6);
    f[12] = ETH_IP >> 8;
    f[13] = ETH_IP & 0xFF;

    f[14] = 0x45;                         /* version 4, ihl 5 (20 bytes) */
    f[15] = 0;                            /* tos */
    f[16] = (u8)((20 + len) >> 8);        /* total length */
    f[17] = (u8)(20 + len);
    f[18] = (u8)(ip_id >> 8);             /* id: no fragments, any value */
    f[19] = (u8)ip_id++;
    f[20] = 0; f[21] = 0;                 /* flags, offset */
    f[22] = 64;                           /* ttl */
    f[23] = proto;
    f[24] = 0; f[25] = 0;                 /* checksum, filled below */
    memcpy(f + 26, ip_self, 4);
    memcpy(f + 30, dst, 4);
    u16 sum = cksum(f + 14, 20);
    f[24] = (u8)(sum >> 8);
    f[25] = (u8)sum;
    memcpy(f + 34, payload, len);
    nic_tx(f, 34 + len);
}

static void icmp_in(const u8 *c, int len, const u8 src[4]);

/* One inbound datagram: check the envelope, then hand the payload to
 * whichever socket owns the port. The header checksum is verified --
 * answering broken packets is the kind of bug that costs an afternoon
 * to find. */
int ip_in(const u8 *ip, int len) {
    if (len < 20 || (ip[0] >> 4) != 4) return 0;
    int ihl = (ip[0] & 0xF) * 4;
    int total = (ip[2] << 8) | ip[3];
    if (ihl < 20 || total > len || total < ihl) return 0;
    if (cksum(ip, ihl)) return 0;         /* nonzero after the fold: bad */
    if (memcmp(ip + 16, ip_self, 4)) return 0;
    if (ip[9] == IP_PROTO_ICMP)
        icmp_in(ip + ihl, total - ihl, ip + 12);
    else if (ip[9] == IP_PROTO_UDP) {
        if (total - ihl < 4) return 0;         /* no room for the port pair */
        struct sock *s = sock_by_port(IP_PROTO_UDP,
                                      (u16)((ip[ihl + 2] << 8) | ip[ihl + 3]));
        if (s) sock_in_udp(s, ip + ihl, total - ihl, ip + 12);
    } else if (ip[9] == IP_PROTO_TCP) {
        if (total - ihl < 4) return 0;         /* no room for the port pair */
        struct sock *s = sock_by_port(IP_PROTO_TCP,
                                      (u16)((ip[ihl + 2] << 8) | ip[ihl + 3]));
        if (s) sock_in_tcp(s, ip + ihl, total - ihl, ip + 12);
    }
    return 1;
}

/* --- icmp -------------------------------------------------------------------- */

static int ping_ok;

static void icmp_in(const u8 *c, int len, const u8 src[4]) {
    if (len < 8) return;
    if (c[0] == ICMP_ECHO_REPLY && !memcmp(src, ip_gw, 4)) {
        ping_ok = 1;                      /* the boot ping came home */
        return;
    }
    /* Echo requests get mirrored back, capped at 40 bytes: the reply
     * must match the request byte for byte, so a bigger ping is left
     * unanswered rather than answered short. */
    if (c[0] == ICMP_ECHO_REQUEST && len <= 40) {
        u8 r[40];
        memcpy(r, c, len);
        r[0] = ICMP_ECHO_REPLY;
        r[2] = 0; r[3] = 0;
        u16 sum = cksum(r, len);
        r[2] = (u8)(sum >> 8);
        r[3] = (u8)sum;
        ip_tx(src, IP_PROTO_ICMP, r, len);
    }
}

/* Ask the gateway for the time of day, one sequence number. */
void ip_ping_gw(void) {
    u8 c[8];
    c[0] = ICMP_ECHO_REQUEST;
    c[1] = 0;
    c[2] = 0; c[3] = 0;                   /* checksum, filled below */
    c[4] = 0; c[5] = 0x50;                /* id: 'P' */
    c[6] = 0; c[7] = 1;                   /* seq */
    u16 sum = cksum(c, sizeof c);
    c[2] = (u8)(sum >> 8);
    c[3] = (u8)sum;
    ip_tx(ip_gw, IP_PROTO_ICMP, c, sizeof c);
}

int ip_ping_ok(void) {
    return ping_ok;
}
