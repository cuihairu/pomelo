#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "proto.h"

/* The sockets. The one change between the stack below and the app
 * above: nothing in this task answers traffic for itself anymore. A
 * socket is a context -- an address pair, the sequence state, one
 * receive buffer, one parked caller -- and apps drive the contexts by
 * message. Parked requests wake the way the kernel's irq waiters do:
 * the caller sleeps in a message, arriving traffic answers it. No
 * timeouts on accept and recv, so a peer that stalls parks its caller
 * with it; the gap is named, not hidden. */

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

struct sock *sock_by_port(u8 proto, u16 dport) {
    for (int i = 0; i < SOCK_CAPS; i++)
        if (socks[i].used && socks[i].proto == proto && socks[i].lport == dport)
            return &socks[i];
    return 0;
}

/* The refusal names the request that was refused: with several opens and
 * recvs in flight, a bare "no" cannot be traced back to its caller. */
static void refuse(const struct msg *req, const char *why) {
    struct msg r;
    memset(&r, 0, sizeof r);
    r.type = MSG_NET_ERR;
    r.arg0 = -1;
    r.arg1 = req->type;
    r.arg2 = (req->type == MSG_NET_OPEN) ? -1 : req->arg0;
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
    if (cap < 0) cap = 0;                /* IPC length: never a wrap */
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
    s->snd_nxt = s->snd_una;             /* data flies from snd_una */
    tcp_send(s, TCP_ACK | TCP_PSH, s->unacked, s->unacked_len);
    s->snd_nxt = s->snd_una + s->unacked_len;
}

static void sock_feed_udp(struct sock *s, const u8 *d, int n) {
    memcpy(s->rbuf, d, n);               /* a datagram replaces a datagram */
    s->rbuf_len = n;
    s->rbuf_off = 0;
    if (s->have_pending && s->pending_kind == MSG_NET_REPLY_RECV)
        sock_consume(s, s->pending_cap);
}

void sock_in_udp(struct sock *s, const u8 *u, int len, const u8 src[4]) {
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
void sock_in_tcp(struct sock *s, const u8 *t, int len, const u8 src[4]) {
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
void sock_tick(void) {
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

/* A SEND length crosses the IPC trust boundary. m->data holds only
 * MSG_DATA bytes, so a TCP segment (like the UDP branch of sock_api)
 * is bounded by what one message can carry -- clamping to SOCKBUF
 * would let memcpy read past the end of the message on the stack. */
int net_clamp_send_len(int len) {
    if (len < 0) return 0;
    if (len > MSG_DATA) return MSG_DATA;
    return len;
}

/* The door apps knock on. One request in, one answer out -- the fs
 * convention: same shape, arg0 carries the number or a -1. */
void sock_api(struct msg *m) {
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
            int len = m->arg1 < 0 ? 0 : m->arg1;
            if (len > MSG_DATA) len = MSG_DATA;
            udp_tx(s->rip, s->rport, s->lport, (const u8 *)m->data,
                   len);
            reply_send(m, s, len);
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
        s->unacked_len = net_clamp_send_len(m->arg1);
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
