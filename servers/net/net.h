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

#endif
