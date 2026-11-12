#ifndef POMELO_NET_SOCK_H
#define POMELO_NET_SOCK_H

#include "../../kernel/types.h"
#include "../../kernel/ipc/ipc.h"   /* struct msg: the parked request */

/* A socket is a context: an address pair, the sequence state, one
 * receive buffer, one parked caller. The host unit test drives the
 * same struct through the real API, so the definition lives here
 * rather than inside sock.c. */

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

#endif
