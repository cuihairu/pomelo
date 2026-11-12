#ifndef POMELO_NET_PROTO_H
#define POMELO_NET_PROTO_H

#include "net.h"
#include "sock.h"

/* The seams between the net service's modules. net.h is the contract
 * with apps; this header is private to the .c files in this directory.
 * Two of the crossings are deliberately mutual: ip_in dispatches
 * inbound frames into the socket table, and the socket senders wrap
 * their payloads with ip_tx -- the same handshakes the one big file
 * used to spell as forward declarations. */

struct msg;

/* Identity: the wire addresses this task answers for (net.c), plus the
 * hardware addresses learned along the way. */
extern u8   mac[6];                    /* the card's, read at bringup */
extern const u8 ip_self[4], ip_gw[4];  /* the SLIRP constants */
extern u8   arp_mac[6];                /* the gateway's, learned by arp */
extern int  arp_ready;

/* Console helpers (nic.c): the service's voice on the screen. */
void say(const char *s);
void say_ip(const char *pre, const u8 ip[4], const char *post);
void say_mac(const u8 m[6]);

/* nic.c: the e1000. One bringup at boot, one tx for every frame out,
 * and the trio the service loop drives: icr asks what fired, sweep
 * drains the ring, eoi stays with the loop that owns the line. */
void nic_bringup(void);
void nic_tx(const u8 *frame, int len);
void nic_pump(void);                   /* wait, clear the cause, sweep, eoi */
void nic_sweep(void);                  /* drain the rx ring into net_rx */
u32  nic_icr(void);                    /* read (and clear) the cause bits */
u32  nic_irq(void);                    /* the pci-assigned interrupt line */

/* arp.c */
void arp_request(const u8 ip[4]);
void arp_in(const u8 *a);              /* one inbound arp frame */

/* ip.c */
u16  cksum(const u8 *b, int n);        /* rfc 1071, odd tail zero-padded */
void ip_tx(const u8 dst[4], u8 proto, const u8 *payload, int len);
int  ip_in(const u8 *ip, int len);     /* 1 if the datagram was ours */
void ip_ping_gw(void);
int  ip_ping_ok(void);

/* sock.c */
struct sock *sock_alloc(void);         /* a free seat, zeroed and marked */
void sock_eof_reply(int dst, int sid); /* the peer is gone: tell the reader */
struct sock *sock_by_port(u8 proto, u16 dport);
void sock_in_udp(struct sock *s, const u8 *u, int len, const u8 src[4]);
void sock_in_tcp(struct sock *s, const u8 *t, int len, const u8 src[4]);
void sock_api(struct msg *m);          /* the door apps knock on */
void sock_tick(void);                  /* the heartbeat half */
int  net_clamp_send_len(int len);      /* the SEND trust boundary */

/* net.c: every received frame lands here */
void net_rx(const u8 *f, int len);

#endif
