#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "proto.h"

/* The net service: e1000 driver plus the small protocol stack, one ring
 * 3 task. The code lives in five files that read bottom-up like the
 * chapters do: nic.c holds the card, arp.c the address lookups, ip.c
 * the datagrams and icmp, sock.c the socket table the apps drive, and
 * this file the identity, the frame dispatch and the service loop.
 * Stage 1 drove bare frames; arp, ipv4 and icmp came next. Stage 5
 * turned the stack into a service other tasks can use: a table of
 * sockets -- address pairs, sequence state, one receive buffer each --
 * that apps drive by message (servers/net/net.h), and the application
 * logic moved out to apps/netsh. Everything arrives in one service
 * loop: sleep on the irq, drain the inbox, clear the card's cause,
 * sweep the rx ring, dispatch each frame by ethertype. */

/* SLIRP's user network (qemu -netdev user): the guest is 10.0.2.15, the
 * gateway 10.0.2.2 -- and under SLIRP every peer appears on-link through
 * the gateway, so one arp slot covers every address this stage talks
 * to. A real system would learn its own address from DHCP; qemu answers
 * the same way every time, so we take the constant. */
const u8 ip_self[4] = { 10, 0, 2, 15 };
const u8 ip_gw[4]   = { 10, 0, 2, 2 };

/* --- ethernet dispatch -------------------------------------------------------- */

void net_rx(const u8 *f, int len) {
    if (len < 14) return;
    u16 et = (u16)((f[12] << 8) | f[13]);
    if (et == ETH_ARP) {
        if (len < ETH_HDR + 28) return;      /* runt: arp_in reads 28 */
        arp_in(f + 14);
    }
    else if (et == ETH_IP)
        (void)ip_in(f + 14, len - 14);
}

/* --- the service --------------------------------------------------------------- */

void net_main(void);
__attribute__((section(".hdr")))
const unsigned net_entry = (unsigned)net_main;

void net_main(void) {
    nic_bringup();

    say("net: e1000 up, mac ");
    say_mac(mac);

    /* Bring the gateway's mac in: ask, then pump until the reply lands.
     * Each round trip raises at least one irq (our own tx write-back),
     * so a lost answer gets re-asked after a few pumps -- and nothing
     * here can take the task down. */
    arp_request(ip_gw);
    for (int i = 0; i < 100 && !arp_ready; i++) {
        if (i % 20 == 19) arp_request(ip_gw);
        nic_pump();
    }
    if (arp_ready) {
        say_ip("net: arp ", ip_gw, " is-at ");
        say_mac(arp_mac);
    }

    /* One icmp echo round trip with the gateway: request out, reply in,
     * both visible in the filter-dump pcap. */
    ip_ping_gw();
    for (int i = 0; i < 100 && !ip_ping_ok(); i++) {
        if (i % 20 == 19) ip_ping_gw();
        nic_pump();
    }
    if (ip_ping_ok())
        say_ip("net: ping ", ip_gw, " ok");

    /* Then it is just the service. The wait mask adds the heartbeat:
     * idle, nobody knocks, but a parked tcp segment still wants its
     * retransmit beat, and only the timer delivers one. A message can
     * wake the wait too, so the inbox is drained first -- the same
     * order as every other task here: never sleep with unread mail. */
    u32 irq = nic_irq();
    for (;;) {
        sys_irq_wait((1u << irq) | 1u);
        struct msg m;
        while (sys_try_recv(&m) == 0)     /* the door: open, accept, send */
            sock_api(&m);
        if (nic_icr()) {
            nic_sweep();                  /* the card spoke */
        } else {
            sock_tick();                  /* the heartbeat spoke */
        }
        sys_irq_eoi(irq);
    }
}
