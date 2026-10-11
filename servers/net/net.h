#ifndef POMELO_NET_H
#define POMELO_NET_H

#include "../../kernel/types.h"

/* The net service: e1000 driver and protocol stack in one ring 3 task.
 * The kernel owns none of the hardware -- PCI probing, MMIO and the PIC
 * line all work the way the fs task reaches its ATA ports: born with
 * IOPL=3 (see boot/main.c). What the kernel does own is the interrupt
 * plumbing: SYS_IRQ_ENABLE opens the PIC line, and the arrival of the
 * irq wakes us through irq_wait, the same primitive any driver uses. */

#define TID_NET 5

/* socket protocol. The net task holds the cards and the protocol; an app
 * that wants traffic asks by message and gets one reply message back per
 * request. Everything below goes over ipc, and every reply reuses the
 * fs convention: same shape, arg0 carries len or a socket, -1 is no. */
#define MSG_NET_OPEN   1  /* arg0=proto, arg1=local port, data: dst ip[4]
                            dst port[2]; a zero dst opens a listener */
#define MSG_NET_ACCEPT 2  /* arg0=socket: park here until one peer lands */
#define MSG_NET_RECV   3  /* arg0=socket, arg1=cap */
#define MSG_NET_SEND   4  /* arg0=socket, arg1=len, data=payload */
#define MSG_NET_CLOSE  5  /* arg0=socket */
#define MSG_NET_ERR    6  /* any request: data=reason, the answer is no */
#define MSG_NET_REPLY_OPEN   7  /* arg0=socket|-1 */
#define MSG_NET_REPLY_ACCEPT 8  /* arg0=socket|-1, data: peer ip[4] port[2] */
#define MSG_NET_REPLY_RECV   9  /* arg0=len, arg1=-1 when the peer ended,
	                            arg2=socket -- replies name their socket,
	                            an app juggling several needs no guesses */
#define MSG_NET_REPLY_SEND   10 /* arg0=len sent, or -1, arg2=socket */
#define NET_CAP 512             /* the largest segment the stack will carry */

/* --- PCI configuration space ------------------------------------------ */
#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E    /* 82540EM, what QEMU's `-device e1000` is */

/* --- e1000 MMIO register offsets (8254x SDM map) ------------------------- */
#define E1000_CTRL   0x00000
#define E1000_STATUS 0x00008
#define E1000_ICR    0x000C0   /* read clears all pending interrupts */
#define E1000_IMS    0x000D0   /* interrupt mask set */
#define E1000_RCTL   0x00100
#define E1000_TCTL   0x00400
#define E1000_RDBAL  0x02800
#define E1000_RDBAH  0x02804
#define E1000_RDLEN  0x02808
#define E1000_RDH    0x02810
#define E1000_RDT    0x02818
#define E1000_TDBAL  0x03800
#define E1000_TDBAH  0x03804
#define E1000_TDLEN  0x03808
#define E1000_TDH    0x03810
#define E1000_TDT    0x03818
#define E1000_RAL    0x05400
#define E1000_RAH    0x05404
#define E1000_TPT    0x040D4   /* tx packets, read clears */

#define CTRL_RST (1u << 26)    /* self-clearing; the emulator honors it */
#define RAH_AV   (1u << 31)
#define IMS_TXDW (1u << 0)     /* tx descriptor written back */
#define IMS_RXT0 (1u << 7)     /* rx timer: frames arrived */
#define RCTL_EN  (1u << 1)
#define RCTL_BAM (1u << 15)    /* accept broadcast */
#define TCTL_EN  (1u << 1)
#define TCTL_PSP (1u << 3)     /* pad short frames */

/* 16-byte descriptors, layout per the e1000 manual. */
struct rx_desc {
    u64 buf;
    u16 len;
    u16 csum;
    u8  status;                /* bit 0: done */
    u8  errors;
    u16 special;
} __attribute__((packed));

struct tx_desc {
    u64 buf;
    u16 len;
    u8  cso;
    u8  cmd;                   /* bit 0: EOP, bit 3: report status */
    u8  sta;                   /* bit 0: done */
    u8  css;
    u16 special;
} __attribute__((packed));

#define RX_DESCS 16
#define TX_DESCS 8              /* ring bytes must be a multiple of 128 */
#define RX_BUF   2048

/* --- ethernet ----------------------------------------------------------- */
#define ETH_HDR      14
#define ETH_BROADCAST ((const u8 *)"\xff\xff\xff\xff\xff\xff")
#define ETH_ARP      0x0806
#define ETH_IP       0x0800

/* ip protocol numbers, the ones this stack understands */
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17
#define IP_PROTO_TCP  6

/* icmp types */
#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

#endif
