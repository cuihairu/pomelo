#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "net.h"

/* The net service: e1000 driver plus the small protocol stack, one ring
 * 3 task. Stage 1 drove bare frames; this file then added arp, ipv4 and
 * icmp. Stage 5 turns the stack into a service other tasks can use: a
 * table of sockets -- address pairs, sequence state, one receive buffer
 * each -- that apps drive by message (servers/net/net.h), and the
 * application logic moved out to apps/netsh. Everything arrives in one
 * service loop: sleep on the irq, drain the inbox, clear the card's
 * cause, sweep the rx ring, dispatch each frame by ethertype. */

static void net_rx(const u8 *f, int len);

static volatile u32 *mmio;        /* the card's MMIO block, from PCI BAR0 */
static int  irq;                  /* its interrupt line, from PCI config */
static u8   mac[6];

static struct rx_desc rx_ring[RX_DESCS] __attribute__((aligned(16)));
static struct tx_desc tx_ring[TX_DESCS] __attribute__((aligned(16)));
static u8 rx_bufs[RX_DESCS][RX_BUF] __attribute__((aligned(16)));
static u8 tx_buf[RX_BUF] __attribute__((aligned(16)));
static int rx_head;

/* The card DMAs through machine addresses, but this task only ever sees
 * its own translation; sys_v2p is the bridge. These hold the answers. */
static u32 rx_ring_pa, tx_ring_pa, tx_buf_pa;
static u32 rx_buf_pa[RX_DESCS];

/* SLIRP's user network (qemu -netdev user): the guest is 10.0.2.15, the
 * gateway 10.0.2.2 -- and under SLIRP every peer appears on-link through
 * the gateway, so one arp slot covers every address this stage talks
 * to. A real system would learn its own address from DHCP; qemu answers
 * the same way every time, so we take the constant. */
static const u8 ip_self[4] = { 10, 0, 2, 15 };
static const u8 ip_gw[4]   = { 10, 0, 2, 2 };

static u8 arp_mac[6];
static int arp_ready;

/* --- mmio ------------------------------------------------------------------ */

static u32 rd(u32 reg)         { return mmio[reg / 4]; }
static void wr(u32 reg, u32 v) { mmio[reg / 4] = v; }

/* --- pci ------------------------------------------------------------------- */

static u32 pci_read(u8 bus, u8 dev, u8 func, u8 reg) {
    outl(PCI_ADDR, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
         ((u32)func << 8) | (reg & ~3u));
    return inl(PCI_DATA);
}

static void pci_write(u8 bus, u8 dev, u8 func, u8 reg, u32 val) {
    outl(PCI_ADDR, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
         ((u32)func << 8) | (reg & ~3u));
    outl(PCI_DATA, val);
}

/* --- console helpers -------------------------------------------------------- */

static const char hex[] = "0123456789abcdef";

static void say(const char *s) {
    sys_write(s, strnlen(s, 128));
}

/* One byte in decimal ("10", "0", "255"), returns the width used. */
static int u8dec(u8 v, char *out) {
    int n = 0;
    if (v >= 100) out[n++] = (char)('0' + v / 100);
    if (v >= 10)  out[n++] = (char)('0' + (v / 10) % 10);
    out[n++] = (char)('0' + v % 10);
    return n;
}

/* "net: arp 10.0.2.2 is-at " -- dotted quad between two text pieces. */
static void say_ip(const char *pre, const u8 ip[4], const char *post) {
    char line[80];
    int p = 0;
    for (const char *s = pre; *s; s++) line[p++] = *s;
    for (int i = 0; i < 4; i++) {
        if (i) line[p++] = '.';
        p += u8dec(ip[i], line + p);
    }
    for (const char *s = post; *s; s++) line[p++] = *s;
    line[p++] = '\n';
    sys_write(line, p);
}

/* "52:55:0a:00:02:02\n" -- the colon form, with its own newline. */
/* "52:55:0a:00:02:02\n" -- the colon form, with its own newline. */
static void say_mac(const u8 m[6]) {
    char line[20];
    int p = 0;
    for (int i = 0; i < 6; i++) {
        if (i) line[p++] = ':';
        line[p++] = hex[m[i] >> 4];
        line[p++] = hex[m[i] & 0xF];
    }
    line[p++] = '\n';
    sys_write(line, p);
}

/* --- e1000 driver ------------------------------------------------------------ */

/* The MAC. The manual's route is the eeprom: ask EERD, wait for done.
 * This emulator takes the request and answers with the register's own
 * stored value -- the read side never completes (verified against the
 * running binary), so the eeprom route is a dead end here. The same
 * address also sits in the receive-filter registers RAL/RAH, which the
 * emulator fills from the device's mac= property; we read it from there
 * and write it back, which is what a real card needs anyway -- its
 * reset clears the filter. */
static void read_mac(void) {
    u32 ral = rd(E1000_RAL), rah = rd(E1000_RAH);
    mac[0] = (u8)ral;         mac[1] = (u8)(ral >> 8);
    mac[2] = (u8)(ral >> 16); mac[3] = (u8)(ral >> 24);
    mac[4] = (u8)rah;         mac[5] = (u8)(rah >> 8);
}

/* Fill descriptor 0 and ring the doorbell; the write to TDT is what
 * starts the hardware. One descriptor in flight is enough for this
 * stack: the next frame waits for this one to land. */
static void net_tx(const u8 *frame, int len) {
    memcpy(tx_buf, frame, len);
    tx_ring[0].buf = tx_buf_pa;
    tx_ring[0].len = len;
    tx_ring[0].cmd = 0x09;                /* EOP | report status */
    tx_ring[0].sta = 0;
    wr(E1000_TDH, 0);                     /* rewind: descriptor 0 is next */
    wr(E1000_TDT, 1);
    for (int i = 0; i < 200000 && !(tx_ring[0].sta & 1); i++)
        sys_yield();                      /* the completion irq lands too */
    tx_ring[0].sta = 0;
}

/* The service loop, opened up: sleep until the line fires, clear the
 * card's interrupt cause, sweep the rx ring, hand each frame to net_rx,
 * then re-arm the pic line. The order is not negotiable: the card's irq
 * output is level-triggered through the pic, so reading ICR -- which
 * drops the line -- must precede the eoi, or the pic re-fires the same
 * level forever. The kernel leaves the eoi of a claimed line to its
 * owner for exactly this reason.
 * (Received frames keep the 4-byte FCS unless RCTL asks otherwise, so
 * rx len is a ceiling, never an exact size; the parsers below trust the
 * header fields instead.) */
/* The rx ring, drained: every descriptor the card has filled goes to
 * net_rx, then the tail is pushed so the card may reuse the slot. */
static void sweep(void) {
    while (rx_ring[rx_head].status & 1) {
        net_rx(rx_bufs[rx_head], rx_ring[rx_head].len);
        rx_ring[rx_head].status = 0;
        rx_head = (rx_head + 1) % RX_DESCS;
        wr(E1000_RDT, (rx_head + RX_DESCS - 1) % RX_DESCS);
    }
}

static void pump(void) {
    sys_irq_wait(1u << irq);
    (void)rd(E1000_ICR);                  /* quiet the card, line falls */
    sweep();
    sys_irq_eoi(irq);                     /* now the pic may re-arm */
}

static void net_bringup(void) {
    /* PCI probe: scan bus 0 for the 82540EM. The config ports answer an
     * iopl-3 task the same way the ata ports answer fs. */
    u32 bar = 0;
    for (int d = 0; d < 32 && !bar; d++) {
        u32 id = pci_read(0, d, 0, 0);
        if ((id & 0xFFFF) == E1000_VENDOR && (id >> 16) == E1000_DEVICE) {
            bar = pci_read(0, d, 0, 0x10) & ~0xFu;
            irq = (int)(pci_read(0, d, 0, 0x3C) & 0xFF);
            /* Firmware normally turns the card on before the OS runs;
             * qemu's direct kernel boot runs no firmware, so we are it:
             * decode io+memory and, above all, open bus mastering --
             * without that bit every DMA read the card does comes back
             * as zeros. */
            pci_write(0, d, 0, 0x04, 0x7);
        }
    }
    if (!bar) {
        say("net: no e1000 on the pci bus\n");
        return;
    }

    /* The card's MMIO block sits near the top of the 32-bit space, far
     * outside the identity-mapped 16 MB -- only the kernel can put it in
     * this task's page tables. Map the register window (32 KB covers
     * every register this driver touches), then take the window's base
     * as our mmio pointer. */
    for (u32 p = bar; p < bar + 0x8000; p += 4096)
        if (!sys_mmio(p)) {
            say("net: mmio map failed\n");
            return;
        }
    mmio = (u32 *)sys_mmio(bar);

    /* Global reset, then wait for the self-clearing bit to fall. On the
     * emulator it falls at once; a card that never clears it must not
     * take the task down with it, so the wait is bounded. */
    wr(E1000_CTRL, rd(E1000_CTRL) | CTRL_RST);
    for (int i = 0; i < 100000; i++)
        if (!(rd(E1000_CTRL) & CTRL_RST))
            break;

    /* Machine addresses for everything the card will read or write. */
    rx_ring_pa = sys_v2p((u32)rx_ring);
    tx_ring_pa = sys_v2p((u32)tx_ring);
    tx_buf_pa  = sys_v2p((u32)tx_buf);
    for (int i = 0; i < RX_DESCS; i++)
        rx_buf_pa[i] = sys_v2p((u32)rx_bufs[i]);

    read_mac();
    /* The receive filter must know the address or every unicast frame
     * aimed at us is dropped before the ring ever sees it (that drop
     * looks exactly like a dead rx path -- ask the tcpdump that found
     * nothing). RAL holds bytes 0..3, RAH bytes 4..5 plus the valid bit;
     * the mac split earlier read it the same way. */
    wr(E1000_RAL, mac[0] | ((u32)mac[1] << 8) |
                  ((u32)mac[2] << 16) | ((u32)mac[3] << 24));
    wr(E1000_RAH, mac[4] | ((u32)mac[5] << 8) | RAH_AV);

    /* Rings before enables: writing RCTL/TCTL also resets the ring
     * pointers, so the enables go last, after every pointer is set. */
    for (int i = 0; i < RX_DESCS; i++) {
        rx_ring[i].buf = rx_buf_pa[i];
        rx_ring[i].len = RX_BUF;
    }
    wr(E1000_RDBAL, rx_ring_pa);
    wr(E1000_RDBAH, 0);
    wr(E1000_RDLEN, RX_DESCS * 16);
    wr(E1000_RDH, 0);
    wr(E1000_RDT, RX_DESCS - 1);
    wr(E1000_TDBAL, tx_ring_pa);
    wr(E1000_TDBAH, 0);
    wr(E1000_TDLEN, TX_DESCS * 16);
    wr(E1000_TDH, 0);
    wr(E1000_TDT, 0);

    wr(E1000_RCTL, RCTL_EN | RCTL_BAM);
    wr(E1000_TCTL, TCTL_EN | TCTL_PSP);
    wr(E1000_IMS, IMS_RXT0 | IMS_TXDW);
    sys_irq_enable(irq);
    /* The card may carry interrupt causes from before we got here (link
     * status settles on its own while the line is still masked). Reading
     * ICR once drops them, so the first thing pump sees is a fresh
     * event, not the card's stale history. */
    (void)rd(E1000_ICR);
}

/* --- arp --------------------------------------------------------------------- */

/* Who has this ip? Broadcast the question; the answer lands in pump.
 * 42 bytes: arp needs no minimum-size pad for this peer, and shorter
 * frames leave more of the one tx buffer alone. */
static void arp_request(const u8 ip[4]) {
    u8 f[42];                             /* 14 header + 28 arp */
    memcpy(f, ETH_BROADCAST, 6);
    memcpy(f + 6, mac, 6);
    f[12] = ETH_ARP >> 8;
    f[13] = ETH_ARP & 0xFF;
    f[14] = 0; f[15] = 1;                 /* hardware type: ethernet */
    f[16] = 0x08; f[17] = 0;              /* protocol type: ipv4 */
    f[18] = 6; f[19] = 4;                 /* hlen, plen */
    f[20] = 0; f[21] = 1;                 /* operation: request */
    memcpy(f + 22, mac, 6);               /* sender hardware address */
    memcpy(f + 28, ip_self, 4);           /* sender protocol address */
    memset(f + 32, 0, 6);                 /* target hardware: unknown */
    memcpy(f + 38, ip, 4);                /* target protocol: the question */
    net_tx(f, sizeof f);
}

/* An arp frame came in. Replies fill the cache; requests for our own
 * address get an answer -- that is what makes us pingable later. */
static void arp_in(const u8 *a) {
    u16 op = (u16)((a[6] << 8) | a[7]);
    if (op == 2 && !memcmp(a + 14, ip_gw, 4)) {   /* reply from the gate */
        memcpy(arp_mac, a + 8, 6);
        arp_ready = 1;
        return;
    }
    if (op == 1 && !memcmp(a + 24, ip_self, 4)) { /* who has us? */
        u8 f[42];
        memcpy(f, a + 8, 6);              /* reply to the asker's mac */
        memcpy(f + 6, mac, 6);
        f[12] = ETH_ARP >> 8;
        f[13] = ETH_ARP & 0xFF;
        f[14] = 0; f[15] = 1;
        f[16] = 0x08; f[17] = 0;
        f[18] = 6; f[19] = 4;
        f[20] = 0; f[21] = 2;             /* operation: reply */
        memcpy(f + 22, mac, 6);
        memcpy(f + 28, ip_self, 4);
        memcpy(f + 34, a + 8, 6);         /* the asker becomes the target */
        memcpy(f + 40, a + 14, 4);
        net_tx(f, sizeof f);
    }
}

/* --- ipv4 -------------------------------------------------------------------- */

/* The one's-complement checksum over n (even) bytes, per rfc 1071. */
static u16 cksum(const u8 *b, int n) {
    u32 s = 0;
    for (int i = 0; i < n; i += 2)
        s += (u32)((b[i] << 8) | b[i + 1]);
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return (u16)(~s & 0xFFFF);
}

static u16 ip_id;

/* Send one ip datagram to the gateway (stage 2's only peer): wrap the
 * payload in a 20-byte header and the cached arp mac. Fragmentation,
 * options and tos are all left out -- the frames this stack sends fit
 * one descriptor, and one peer needs no routing decision. */
static void ip_tx(const u8 dst[4], u8 proto, const u8 *payload, int len) {
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
    net_tx(f, 34 + len);
}

static void icmp_in(const u8 *c, int len, const u8 src[4]);
struct sock;
static struct sock *sock_by_port(u8 proto, u16 dport);
static void sock_in_udp(struct sock *s, const u8 *u, int len, const u8 src[4]);
static void sock_in_tcp(struct sock *s, const u8 *t, int len, const u8 src[4]);

/* One inbound datagram: check the envelope, then hand the payload to
 * whichever socket owns the port. The header checksum is verified --
 * answering broken packets is the kind of bug that costs an afternoon
 * to find. */
static void ip_in(const u8 *ip, int len) {
    if (len < 20 || (ip[0] >> 4) != 4) return;
    int ihl = (ip[0] & 0xF) * 4;
    int total = (ip[2] << 8) | ip[3];
    if (ihl < 20 || total > len || total < ihl) return;
    if (cksum(ip, ihl)) return;           /* nonzero after the fold: bad */
    if (memcmp(ip + 16, ip_self, 4)) return;
    if (ip[9] == IP_PROTO_ICMP)
        icmp_in(ip + ihl, total - ihl, ip + 12);
    else if (ip[9] == IP_PROTO_UDP) {
        struct sock *s = sock_by_port(IP_PROTO_UDP,
                                      (u16)((ip[ihl + 2] << 8) | ip[ihl + 3]));
        if (s) sock_in_udp(s, ip + ihl, total - ihl, ip + 12);
    } else if (ip[9] == IP_PROTO_TCP) {
        struct sock *s = sock_by_port(IP_PROTO_TCP,
                                      (u16)((ip[ihl + 2] << 8) | ip[ihl + 3]));
        if (s) sock_in_tcp(s, ip + ihl, total - ihl, ip + 12);
    }
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
static void ping_gw(void) {
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

/* --- sockets ------------------------------------------------------------------ */

/* The one change between the stack below and the app above: nothing in
 * this task answers traffic for itself anymore. A socket is a context --
 * an address pair, the sequence state, one receive buffer, one parked
 * caller -- and apps drive the contexts by message. Parked requests
 * wake the way the kernel's irq waiters do: the caller sleeps in a
 * message, arriving traffic answers it. No timeouts on accept and recv,
 * so a peer that stalls parks its caller with it; the gap is named, not
 * hidden. */

#define SOCK_CAPS 4
#define SOCKBUF  512

struct sock {
    int  used;
    int  proto;                     /* IP_PROTO_TCP or IP_PROTO_UDP */
    u8   rip[4];                    /* the peer: learned on accept, given on open */
    u16  lport, rport;
    int  state;                     /* TS_* */
    u32  rcv_nxt;                   /* the next byte we expect */
    u32  iss, snd_nxt, snd_una;     /* our side: init, next, oldest unacked */
    u8   unacked[SOCKBUF];          /* one segment out, parked for its ack */
    int  unacked_len;
    int  retx_ticks, retx_count;
    int  close_wait;                /* close asked, our data not acked yet */
    int  fin_seen;                  /* the peer has ended its side */
    u8   rbuf[SOCKBUF];             /* arrived bytes, waiting for a reader */
    int  rbuf_len, rbuf_off;
    struct msg pending;             /* the parked open, accept or recv */
    int  pending_kind;              /* which reply that caller expects */
    int  pending_cap;
    int  have_pending;
};

static struct sock socks[SOCK_CAPS];
static u16  eph_port;               /* local ports for active opens */

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

enum { TS_LISTEN, TS_SYN_SENT, TS_SYN_RCVD, TS_ESTABLISHED };

/* Retransmit: a parked segment flies again until its ack comes home --
 * 50 heartbeats (~0.5 s) apart, eight times. The ninth failure writes
 * the connection off, and whatever was parked on it hears the no rather
 * than sitting in a message until the machine reboots. */
#define TCP_RETX_TICKS 50
#define TCP_RETX_LIMIT 8

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* The small print: this is a minimal tcp on purpose. No options (no
 * mss, no window scale), no sack, no congestion control, no out-of-order
 * receive, no time_wait, one connection per socket, a fixed isn and a
 * fixed window. The gaps are safe here because the peer is a test tool
 * on the host, not the internet: segments arrive in order or the
 * dup-ack asks for a resend, and a classroom peer does not guess at the
 * isn. */

/* The rfc 768 checksum covers a pseudo-header (ips, protocol, length)
 * on top of the datagram; rfc 793's tcp uses the same scheme. IPv4
 * permits sending zero for udp -- we fold the real thing anyway,
 * because a wrong-checksum datagram is exactly the kind of bug that
 * costs an afternoon to find. */
static u16 pseudo_cksum(const u8 src[4], const u8 dst[4], u8 proto,
                        const u8 *d, int len) {
    u32 s = 0;
    for (int i = 0; i < 4; i += 2)
        s += (u32)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 4; i += 2)
        s += (u32)((dst[i] << 8) | dst[i + 1]);
    s += (u32)proto + (u32)len;
    for (int i = 0; i < len; i += 2) {
        u16 hi = d[i];
        u16 lo = (i + 1 < len) ? d[i + 1] : 0;   /* odd tail pads zero */
        s += (u32)((hi << 8) | lo);
    }
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return (u16)(~s & 0xFFFF);
}

/* Wrap a payload and send it out. No fragmentation -- datagrams that do
 * not fit the echo buffer are refused at the door, not cut short. */
static void udp_tx(const u8 dst[4], u16 dport, u16 sport,
                   const u8 *payload, int len) {
    static u8 d[8 + 512];
    if (!arp_ready || len > 512)
        return;
    d[0] = (u8)(sport >> 8); d[1] = (u8)sport;
    d[2] = (u8)(dport >> 8); d[3] = (u8)dport;
    d[4] = (u8)((8 + len) >> 8); d[5] = (u8)(8 + len);
    d[6] = 0; d[7] = 0;                   /* checksum, filled below */
    memcpy(d + 8, payload, len);
    u16 sum = pseudo_cksum(ip_self, dst, IP_PROTO_UDP, d, 8 + len);
    d[6] = (u8)(sum >> 8);
    d[7] = (u8)sum;
    ip_tx(dst, IP_PROTO_UDP, d, 8 + len);
}

static struct sock *sock_alloc(void) {
    for (int i = 0; i < SOCK_CAPS; i++) {
        if (socks[i].used) continue;
        memset(&socks[i], 0, sizeof socks[i]);
        socks[i].used = 1;
        return &socks[i];
    }
    return 0;
}

static int sock_id(const struct sock *s) {
    return (int)(s - socks);
}

static struct sock *sock_by_port(u8 proto, u16 dport) {
    for (int i = 0; i < SOCK_CAPS; i++)
        if (socks[i].used && socks[i].proto == proto && socks[i].lport == dport)
            return &socks[i];
    return 0;
}

static void refuse(const struct msg *req, const char *why) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_ERR;
    r.arg0 = -1;
    for (int i = 0; why[i] && i < MSG_DATA - 1; i++)
        r.data[i] = why[i];
    sys_send_wait(req->src, &r);
}

/* arg1 carries the local port: an app that asked for several opens at
 * once tells the answers apart by it (sock_wake does the same). */
static void reply_open(const struct msg *req, const struct sock *s) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_REPLY_OPEN;
    r.arg0 = sock_id(s);
    r.arg1 = s->lport;
    sys_send_wait(req->src, &r);
}

static void reply_send(const struct msg *req, const struct sock *s, int n) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_REPLY_SEND;
    r.arg0 = n;
    r.arg2 = sock_id(s);
    sys_send_wait(req->src, &r);
}

/* A reader that asked before the bytes landed hears the end: nothing
 * left, and the peer has said goodbye. */
static void say_eof(const struct sock *s) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_REPLY_RECV;
    r.arg0 = 0;
    r.arg1 = -1;
    r.arg2 = sock_id(s);
    sys_send_wait(s->pending.src, &r);
}

/* Every parked caller gets exactly one answer, then the seat is free.
 * A failure wakes a recv as an end (arg0 -1 says it was not clean), an
 * accept or open as a plain no. */
static void sock_wake(struct sock *s, int fail) {
    if (!s->have_pending) return;
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = s->pending_kind;
    if (s->pending_kind == MSG_NET_REPLY_RECV) {
        r.arg0 = fail ? -1 : 0;
        r.arg1 = -1;
        r.arg2 = sock_id(s);
    } else {
        r.arg0 = fail ? -1 : sock_id(s);
        r.arg1 = s->lport;
    }
    if (!fail && s->pending_kind == MSG_NET_REPLY_ACCEPT) {
        memcpy(r.data, s->rip, 4);
        r.data[4] = (u8)(s->rport >> 8);
        r.data[5] = (u8)s->rport;
    }
    s->have_pending = 0;
    sys_send_wait(s->pending.src, &r);
}

/* The tear-down: no caller may outlive its socket. */
static void sock_drop(struct sock *s) {
    sock_wake(s, 1);
    s->used = 0;
}

static void sock_park(struct sock *s, const struct msg *req, int kind, int cap) {
    s->pending = *req;
    s->pending_kind = kind;
    s->pending_cap = cap;
    s->have_pending = 1;
}

/* Hand buffered bytes to whoever asked. A recv never invents padding:
 * it delivers the smaller of what is there and what was asked, and the
 * rest stays for the next call. */
static void sock_consume(struct sock *s, int cap) {
    int n = s->rbuf_len;
    if (n > cap) n = cap;
    if (n > MSG_DATA) n = MSG_DATA;
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_REPLY_RECV;
    r.arg0 = n;
    r.arg2 = sock_id(s);
    if (n) memcpy(r.data, s->rbuf + s->rbuf_off, n);
    s->rbuf_off += n;
    s->rbuf_len -= n;
    if (!s->rbuf_len) s->rbuf_off = 0;
    s->have_pending = 0;
    sys_send_wait(s->pending.src, &r);
}

/* One segment out. The ack field always carries rcv_nxt -- no delayed
 * acks; the window is a fixed 8192 because every segment is processed
 * the moment it lands. seq = snd_nxt; the caller advances it. */
static void tcp_send(struct sock *s, u8 flags, const u8 *payload, int len) {
    static u8 t[20 + SOCKBUF];
    if (len > SOCKBUF) return;
    t[0] = s->lport >> 8; t[1] = (u8)s->lport;
    t[2] = s->rport >> 8; t[3] = (u8)s->rport;
    t[4] = (u8)(s->snd_nxt >> 24); t[5] = (u8)(s->snd_nxt >> 16);
    t[6] = (u8)(s->snd_nxt >> 8);  t[7] = (u8)s->snd_nxt;
    t[8] = (u8)(s->rcv_nxt >> 24); t[9] = (u8)(s->rcv_nxt >> 16);
    t[10] = (u8)(s->rcv_nxt >> 8); t[11] = (u8)s->rcv_nxt;
    t[12] = 0x50;                        /* data offset: 5 words, no options */
    t[13] = flags;
    t[14] = 0x20; t[15] = 0;             /* window */
    t[16] = 0; t[17] = 0;                /* checksum, folded below */
    t[18] = 0; t[19] = 0;                /* urgent: no */
    if (len) memcpy(t + 20, payload, len);
    u16 sum = pseudo_cksum(ip_self, s->rip, IP_PROTO_TCP, t, 20 + len);
    t[16] = (u8)(sum >> 8);
    t[17] = (u8)sum;
    ip_tx(s->rip, IP_PROTO_TCP, t, 20 + len);
}

static void sock_send_seg(struct sock *s) {
    u32 save = s->snd_nxt;               /* data flies from snd_una */
    s->snd_nxt = s->snd_una;
    tcp_send(s, TCP_ACK | TCP_PSH, s->unacked, s->unacked_len);
    s->snd_nxt = save + s->unacked_len;
}

static void sock_feed_udp(struct sock *s, const u8 *d, int n) {
    memcpy(s->rbuf, d, n);               /* a datagram replaces a datagram */
    s->rbuf_len = n;
    s->rbuf_off = 0;
    if (s->have_pending && s->pending_kind == MSG_NET_REPLY_RECV)
        sock_consume(s, s->pending_cap);
}

static void sock_in_udp(struct sock *s, const u8 *u, int len, const u8 src[4]) {
    if (len < 8) return;
    u16 sport = (u16)((u[0] << 8) | u[1]);
    u16 ulen  = (u16)((u[4] << 8) | u[5]);
    if (ulen < 8 || ulen > len) return;
    int plen = ulen - 8;
    if (plen > SOCKBUF) return;
    memcpy(s->rip, src, 4);              /* replies go where it came from */
    s->rport = sport;
    sock_feed_udp(s, u + 8, plen);
}

/* One inbound segment for a live socket. The ladder is the usual one,
 * with the echo logic gone: data lands in the buffer, a parked reader
 * wakes, and what the app does with the bytes is the app's business. */
static void sock_in_tcp(struct sock *s, const u8 *t, int len, const u8 src[4]) {
    if (len < 20) return;
    u16 sport = (u16)((t[0] << 8) | t[1]);
    u32 seq = be32(t + 4);
    u32 ack = be32(t + 8);
    u8 doff = (u8)((t[12] >> 4) * 4);
    if (doff < 20 || doff > len) return;
    u8 flags = t[13];
    int plen = len - doff;

    /* Once a peer is known, only its segments count: a second host
     * knocking on the port is somebody else's call. */
    if (s->state != TS_LISTEN &&
        (memcmp(src, s->rip, 4) || sport != s->rport))
        return;

    if (flags & TCP_RST) {               /* abort, whoever it is from */
        say("net: tcp reset\n");
        sock_drop(s);
        return;
    }

    switch (s->state) {
    case TS_LISTEN:
        if (!(flags & TCP_SYN)) return;
        memcpy(s->rip, src, 4);
        s->rport = sport;
        s->rcv_nxt = seq + 1;
        s->iss = 0x50505050u;            /* 'PPPP': fixed on purpose */
        s->snd_una = s->snd_nxt = s->iss;
        tcp_send(s, TCP_SYN | TCP_ACK, 0, 0);
        s->snd_nxt = s->iss + 1;         /* the syn consumed one number */
        s->state = TS_SYN_RCVD;
        return;

    case TS_SYN_SENT:
        /* The active open's answer. The peer's syn carries its iss; the
         * ack may ride the same segment or follow it. The iss is new
         * news every time -- unlike the established states there is
         * nothing to match it against, so it is taken as it comes. */
        if (!(flags & TCP_SYN)) return;
        s->rcv_nxt = seq + 1;
        s->snd_nxt = s->iss;
        tcp_send(s, TCP_SYN | TCP_ACK, 0, 0);
        s->snd_nxt = s->iss + 1;
        if ((flags & TCP_ACK) && ack == s->snd_nxt) {
            s->snd_una = s->snd_nxt;
            s->state = TS_ESTABLISHED;
            sock_wake(s, 0);
        } else {
            s->state = TS_SYN_RCVD;
        }
        return;

    case TS_SYN_RCVD:
        /* The peer's ack went missing and it is asking again: same
         * syn-ack once more. */
        if ((flags & TCP_SYN) && seq + 1 == s->rcv_nxt) {
            s->snd_nxt = s->iss;
            tcp_send(s, TCP_SYN | TCP_ACK, 0, 0);
            s->snd_nxt = s->iss + 1;
            return;
        }
        if ((flags & TCP_ACK) && ack == s->snd_nxt) {
            s->snd_una = s->snd_nxt;
            s->state = TS_ESTABLISHED;
            sock_wake(s, 0);
        }
        return;

    case TS_ESTABLISHED:
        if (plen > 0 && seq == s->rcv_nxt) {         /* in-order data */
            if (s->rbuf_len && s->rbuf_off) {        /* compact the tail */
                memmove(s->rbuf, s->rbuf + s->rbuf_off, s->rbuf_len);
                s->rbuf_off = 0;
            }
            if (s->rbuf_len + plen > SOCKBUF) {
                tcp_send(s, TCP_ACK, 0, 0);          /* full: ask again later */
            } else {
                memcpy(s->rbuf + s->rbuf_off + s->rbuf_len, t + doff, plen);
                s->rbuf_len += plen;
                s->rcv_nxt += plen;
                if (s->have_pending && s->pending_kind == MSG_NET_REPLY_RECV)
                    sock_consume(s, s->pending_cap);
            }
        } else if (plen > 0 && seq + plen <= s->rcv_nxt) {
            tcp_send(s, TCP_ACK, 0, 0);              /* old news: re-ack it */
            if (!(flags & TCP_FIN)) return;
        } else if (plen > 0) {                       /* a gap: beg for resend */
            tcp_send(s, TCP_ACK, 0, 0);
            return;
        }
        if ((flags & TCP_ACK) && s->unacked_len && ack == s->snd_nxt) {
            s->snd_una = s->snd_nxt;                 /* our segment landed */
            s->unacked_len = 0;
        }
        /* Checked after the data, so a data+fin segment closes in one
         * pass instead of stranding the fin behind an early return. */
        if (flags & TCP_FIN) {
            if (seq + plen == s->rcv_nxt) {
                s->rcv_nxt = seq + plen + 1;
                s->fin_seen = 1;
                /* a reader parked on an empty buffer hears the end now;
                 * one with bytes still queued hears it after the drain */
                if (!s->rbuf_len && s->have_pending &&
                    s->pending_kind == MSG_NET_REPLY_RECV) {
                    s->have_pending = 0;
                    say_eof(s);
                }
            }
            /* our own fin waits for a clear line -- sock_close or the
             * tick's flush sends it once nothing is unacked */
            tcp_send(s, TCP_ACK, 0, 0);
        }
        return;
    }
}

/* The heartbeat's half: a parked segment flies again until its ack
 * lands, the retries run out, or it is a close waiting for the line to
 * clear. A written-off connection wakes whatever was parked on it. */
static void sock_tick(void) {
    for (int i = 0; i < SOCK_CAPS; i++) {
        struct sock *s = &socks[i];
        if (!s->used) continue;
        int busy = s->unacked_len || s->close_wait ||
                   s->state == TS_SYN_SENT;
        if (!busy) continue;
        if (++s->retx_ticks < TCP_RETX_TICKS) continue;
        s->retx_ticks = 0;
        if (s->state == TS_SYN_SENT) {   /* the active open's syn */
            if (++s->retx_count > TCP_RETX_LIMIT) {
                say("net: connect gave up\n");
                sock_drop(s);
            } else {
                u32 save = s->snd_nxt;
                s->snd_nxt = s->iss;
                tcp_send(s, TCP_SYN, 0, 0);
                s->snd_nxt = save;
            }
            continue;
        }
        if (!s->unacked_len) {           /* only a parked close remains */
            tcp_send(s, TCP_ACK | TCP_FIN, 0, 0);
            sock_drop(s);
            continue;
        }
        if (++s->retx_count > TCP_RETX_LIMIT) {
            say("net: tcp gave up\n");
            sock_drop(s);
            continue;
        }
        sock_send_seg(s);
    }
}

/* The door apps knock on. One request in, one answer out -- the fs
 * convention: same shape, arg0 carries the number or a -1. */
static void sock_api(struct msg *m) {
    struct sock *s = 0;
    if (m->type != MSG_NET_OPEN && m->arg0 >= 0 && m->arg0 < SOCK_CAPS)
        s = socks[m->arg0].used ? &socks[m->arg0] : 0;

    switch (m->type) {
    case MSG_NET_OPEN: {
        u8 dst[4];
        int dport;
        memcpy(dst, m->data, 4);
        dport = (m->data[4] << 8) | m->data[5];
        if (m->arg0 != IP_PROTO_TCP && m->arg0 != IP_PROTO_UDP) {
            refuse(m, "open: proto must be tcp or udp");
            return;
        }
        /* ip_tx reaches exactly one peer, the gateway; an open that
         * names anyone else would fail silently, so it fails loudly. */
        if (dport && memcmp(dst, ip_gw, 4)) {
            refuse(m, "open: only the gateway is reachable");
            return;
        }
        struct sock *n = sock_alloc();
        if (!n) {
            refuse(m, "open: no socket free");
            return;
        }
        n->proto = m->arg0;
        n->lport = (u16)m->arg1;
        if (!dport) {                    /* passive: a listener, at once */
            n->state = TS_LISTEN;
            reply_open(m, n);
            return;
        }
        memcpy(n->rip, dst, 4);
        n->rport = (u16)dport;
        if (n->proto == IP_PROTO_UDP) {  /* udp has no ladder to climb */
            n->state = TS_ESTABLISHED;
            reply_open(m, n);
            return;
        }
        if (!n->lport)                   /* the syn needs a port to land on */
            n->lport = (u16)(40000 + (++eph_port & 0xFFF));
        n->state = TS_SYN_SENT;
        n->iss = 0x50505050u;
        n->snd_una = n->snd_nxt = n->iss;
        n->retx_ticks = n->retx_count = 0;
        tcp_send(n, TCP_SYN, 0, 0);
        n->snd_nxt = n->iss + 1;
        sock_park(n, m, MSG_NET_REPLY_OPEN, 0);
        return;
    }
    case MSG_NET_ACCEPT:
        if (!s || s->proto != IP_PROTO_TCP || s->state != TS_LISTEN) {
            refuse(m, "accept: not a listener");
            return;
        }
        if (s->have_pending) {
            refuse(m, "accept: one is already parked here");
            return;
        }
        sock_park(s, m, MSG_NET_REPLY_ACCEPT, 0);
        return;
    case MSG_NET_RECV:
        if (!s || !s->used) {
            refuse(m, "recv: bad socket");
            return;
        }
        if (s->rbuf_len) {               /* bytes are here: answer at once */
            sock_consume(s, m->arg1);
            return;
        }
        if (s->fin_seen) {               /* drained and done: the end */
            say_eof(s);
            return;
        }
        if (s->have_pending) {
            refuse(m, "recv: one is already parked here");
            return;
        }
        sock_park(s, m, MSG_NET_REPLY_RECV, m->arg1);
        return;
    case MSG_NET_SEND:
        if (!s || !s->used) {
            refuse(m, "send: bad socket");
            return;
        }
        if (s->proto == IP_PROTO_UDP) {  /* fire and forget */
            udp_tx(s->rip, s->rport, s->lport, (const u8 *)m->data,
                   m->arg1 > MSG_DATA ? MSG_DATA : m->arg1);
            reply_send(m, s, m->arg1);
            return;
        }
        if (s->state != TS_ESTABLISHED) {
            refuse(m, "send: not connected");
            return;
        }
        if (s->unacked_len) {
            refuse(m, "send: the last segment is still out");
            return;
        }
        s->unacked_len = m->arg1 > SOCKBUF ? SOCKBUF : m->arg1;
        memcpy(s->unacked, m->data, s->unacked_len);
        s->retx_ticks = s->retx_count = 0;
        sock_send_seg(s);
        reply_send(m, s, s->unacked_len);
        return;
    case MSG_NET_CLOSE:
        if (!s || !s->used) {
            refuse(m, "close: bad socket");
            return;
        }
        if (s->proto == IP_PROTO_UDP) {
            sock_drop(s);
            return;
        }
        if (s->state != TS_ESTABLISHED) {
            sock_drop(s);
            return;
        }
        if (s->unacked_len) {            /* the tick flushes the fin */
            s->close_wait = 1;
            return;
        }
        tcp_send(s, TCP_ACK | TCP_FIN, 0, 0);
        sock_drop(s);
        return;
    }
}

/* --- ethernet dispatch -------------------------------------------------------- */

static void net_rx(const u8 *f, int len) {
    if (len < 14) return;
    u16 et = (u16)((f[12] << 8) | f[13]);
    if (et == ETH_ARP)
        arp_in(f + 14);
    else if (et == ETH_IP)
        ip_in(f + 14, len - 14);
}

/* --- the service --------------------------------------------------------------- */

void net_main(void);
__attribute__((section(".hdr")))
const unsigned net_entry = (unsigned)net_main;

void net_main(void) {
    net_bringup();

    say("net: e1000 up, mac ");
    say_mac(mac);

    /* Bring the gateway's mac in: ask, then pump until the reply lands.
     * Each round trip raises at least one irq (our own tx write-back),
     * so a lost answer gets re-asked after a few pumps -- and nothing
     * here can take the task down. */
    arp_request(ip_gw);
    for (int i = 0; i < 100 && !arp_ready; i++) {
        if (i % 20 == 19) arp_request(ip_gw);
        pump();
    }
    if (arp_ready) {
        say_ip("net: arp ", ip_gw, " is-at ");
        say_mac(arp_mac);
    }

    /* One icmp echo round trip with the gateway: request out, reply in,
     * both visible in the filter-dump pcap. */
    ping_gw();
    for (int i = 0; i < 100 && !ping_ok; i++) {
        if (i % 20 == 19) ping_gw();
        pump();
    }
    if (ping_ok)
        say_ip("net: ping ", ip_gw, " ok");

    /* Then it is just the service. The wait mask adds the heartbeat:
     * idle, nobody knocks, but a parked tcp segment still wants its
     * retransmit beat, and only the timer delivers one. A message can
     * wake the wait too, so the inbox is drained first -- the same
     * order as every other task here: never sleep with unread mail. */
    for (;;) {
        sys_irq_wait((1u << irq) | 1u);
        struct msg m;
        while (sys_try_recv(&m) == 0)     /* the door: open, accept, send */
            sock_api(&m);
        u32 icr = rd(E1000_ICR);          /* quiet the card, line falls */
        if (icr) {
            sweep();                      /* the card spoke */
        } else {
            sock_tick();                  /* the heartbeat spoke */
        }
        sys_irq_eoi(irq);
    }
}
