#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "net.h"

/* The net service: e1000 driver plus the small protocol stack, one ring
 * 3 task. Stage 1 drove bare frames; this file adds arp, ipv4, icmp and
 * a udp echo service. Everything arrives in one service loop (pump):
 * sleep on the irq, clear the card's cause, sweep the rx ring, dispatch
 * each frame by ethertype. The boot sequence rides the same loop --
 * send a request, pump until the answer lands -- so the startup
 * transcript doubles as the proof: the gateway's arp reply and one icmp
 * echo round trip, both real. */

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
static void pump(void) {
    sys_irq_wait(1u << irq);
    (void)rd(E1000_ICR);                  /* quiet the card, line falls */
    while (rx_ring[rx_head].status & 1) {
        net_rx(rx_bufs[rx_head], rx_ring[rx_head].len);
        rx_ring[rx_head].status = 0;
        rx_head = (rx_head + 1) % RX_DESCS;
        wr(E1000_RDT, (rx_head + RX_DESCS - 1) % RX_DESCS);
    }
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
static void udp_in(const u8 *u, int len, const u8 src[4]);

/* One inbound datagram: check the envelope, then hand the payload up.
 * The header checksum is verified -- answering broken packets is the
 * kind of bug that costs an afternoon to find. */
static void ip_in(const u8 *ip, int len) {
    if (len < 20 || (ip[0] >> 4) != 4) return;
    int ihl = (ip[0] & 0xF) * 4;
    int total = (ip[2] << 8) | ip[3];
    if (ihl < 20 || total > len || total < ihl) return;
    if (cksum(ip, ihl)) return;           /* nonzero after the fold: bad */
    if (memcmp(ip + 16, ip_self, 4)) return;
    if (ip[9] == IP_PROTO_ICMP)
        icmp_in(ip + ihl, total - ihl, ip + 12);
    else if (ip[9] == IP_PROTO_UDP)
        udp_in(ip + ihl, total - ihl, ip + 12);
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

/* --- udp ---------------------------------------------------------------------- */

/* The one port this stage serves; the host side of the demo forwards to
 * it with -hostfwd udp::2325-:2325 and talks with nc -u 127.0.0.1 2325. */
#define UDP_ECHO_PORT 2325

/* The rfc 768 checksum covers a pseudo-header (ips, protocol, length)
 * on top of the datagram. IPv4 permits sending zero instead -- we fold
 * the real thing anyway, because a wrong-checksum datagram is exactly
 * the kind of bug that costs an afternoon to find. */
static u16 udp_cksum(const u8 src[4], const u8 dst[4], const u8 *d, int len) {
    u32 s = 0;
    for (int i = 0; i < 4; i += 2)
        s += (u32)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 4; i += 2)
        s += (u32)((dst[i] << 8) | dst[i + 1]);
    s += IP_PROTO_UDP + (u32)len;
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
    u16 sum = udp_cksum(ip_self, dst, d, 8 + len);
    d[6] = (u8)(sum >> 8);
    d[7] = (u8)sum;
    ip_tx(dst, IP_PROTO_UDP, d, 8 + len);
}

/* One inbound datagram. Only the echo port answers, and it answers by
 * mirroring the payload to wherever the datagram came from; other ports
 * are silently not ours. */
static void udp_in(const u8 *u, int len, const u8 src[4]) {
    if (len < 8) return;
    u16 sport = (u16)((u[0] << 8) | u[1]);
    u16 dport = (u16)((u[2] << 8) | u[3]);
    u16 ulen  = (u16)((u[4] << 8) | u[5]);
    if (ulen < 8 || ulen > len) return;
    if (dport != UDP_ECHO_PORT) return;
    int plen = ulen - 8;
    if (plen > 512) return;
    udp_tx(src, sport, UDP_ECHO_PORT, u + 8, plen);
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

    /* Then it is just the service: every pump dispatches what arrived,
     * and the udp echo port answers whoever knocks. */
    say("net: udp echo on 2325\n");
    for (;;) pump();
}
