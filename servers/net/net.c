#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "net.h"

/* Stage 1: the e1000 driver, talking bare ethernet. Probes PCI, turns
 * the card on, reads its MAC, brings up the TX and RX descriptor rings,
 * and sends one ARP request -- the frame that proves the wire works
 * (the host reads it out of a filter-dump pcap). The RX loop counts
 * frames and drops them; the protocol stack hangs off the sweep loop
 * in the stages after this one. */

static u32 *mmio;                 /* the card's MMIO block, from PCI BAR0 */
static int  irq;                  /* its interrupt line, from PCI config */
static u8   mac[6];

static struct rx_desc rx_ring[RX_DESCS] __attribute__((aligned(16)));
static struct tx_desc tx_ring[TX_DESCS] __attribute__((aligned(16)));
static u8 rx_bufs[RX_DESCS][RX_BUF] __attribute__((aligned(16)));
static u8 tx_buf[RX_BUF] __attribute__((aligned(16)));
static int rx_head;
static u32 rx_count, tx_count;

/* The card DMAs through machine addresses, but this task only ever sees
 * its own translation; sys_v2p is the bridge. These hold the answers. */
static u32 rx_ring_pa, tx_ring_pa, tx_buf_pa;
static u32 rx_buf_pa[RX_DESCS];

/* SLIRP's user network (qemu -netdev user): the guest is 10.0.2.15, the
 * gateway 10.0.2.2. A real system would learn both from DHCP; a qemu
 * user network answers them the same way every time, so we take the
 * constants and spend the saved code on the stack itself. */
static const u8 ip_self[4] = { 10, 0, 2, 15 };
static const u8 ip_gw[4]   = { 10, 0, 2, 2 };

/* --- mmio ----------------------------------------------------------------- */

static u32 rd(u32 reg)         { return mmio[reg / 4]; }
static void wr(u32 reg, u32 v) { mmio[reg / 4] = v; }

/* --- pci ------------------------------------------------------------------ */

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

/* --- console helpers ------------------------------------------------------- */

static void say(const char *s) {
    sys_write(s, strnlen(s, 128));
}

/* --- e1000 setup ----------------------------------------------------------- */

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
    wr(E1000_TDT, 1);
    for (int i = 0; i < 200000 && !(tx_ring[0].sta & 1); i++)
        sys_yield();                      /* the completion irq lands too */
    tx_ring[0].sta = 0;
    tx_count++;
}

/* The one bare frame this stage exists to send: who has 10.0.2.2? */
static void arp_probe(void) {
    u8 f[60];                             /* 60: ethernet's minimum pad */
    memset(f, 0, sizeof f);
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
    memcpy(f + 38, ip_gw, 4);             /* target protocol address */
    net_tx(f, sizeof f);
}

void net_main(void);
__attribute__((section(".hdr")))
const unsigned net_entry = (unsigned)net_main;

static void print_mac(void) {
    static const char hex[] = "0123456789abcdef";
    char line[40];
    int p = 0;
    for (const char *s = "net: e1000 up, mac "; *s; s++) line[p++] = *s;
    for (int i = 0; i < 6; i++) {
        if (i) line[p++] = ':';
        line[p++] = hex[mac[i] >> 4];
        line[p++] = hex[mac[i] & 0xF];
    }
    line[p++] = '\n';
    sys_write(line, p);
}

void net_main(void) {
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
    wr(E1000_RAL, mac[0] | ((u32)mac[1] << 8));
    wr(E1000_RAH, mac[2] | ((u32)mac[3] << 8) | RAH_AV);

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
    print_mac();
    arp_probe();

    /* The service loop: sleep until the line fires, clear the card's
     * interrupt state, sweep the rx ring, hand each frame up. */
    for (;;) {
        sys_irq_wait(1u << irq);
        (void)rd(E1000_ICR);
        while (rx_ring[rx_head].status & 1) {
            int len = rx_ring[rx_head].len;
            (void)len;                    /* stage 2 delivers it upward */
            rx_count++;
            rx_ring[rx_head].status = 0;
            rx_head = (rx_head + 1) % RX_DESCS;
            wr(E1000_RDT, (rx_head + RX_DESCS - 1) % RX_DESCS);
        }
    }
}
