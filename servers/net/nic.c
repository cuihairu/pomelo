#include "../../kernel/string.h"
#include "../../kernel/syscall/syscall.h"
#include "proto.h"

/* The card. PCI probing, MMIO mapping, the descriptor rings and the
 * interrupt hand-off live here, behind the handful of calls the rest
 * of the service uses: bringup once at boot, tx for every frame out,
 * and the icr/sweep pair the service loop drives. The console helpers
 * that give every module its voice on the screen are here too -- the
 * driver is the bottom layer, and everything above prints through it. */

static volatile u32 *mmio;        /* the card's MMIO block, from PCI BAR0 */
static int  irq;                  /* its interrupt line, from PCI config */
u8   mac[6];

static struct rx_desc rx_ring[RX_DESCS] __attribute__((aligned(16)));
static struct tx_desc tx_ring[TX_DESCS] __attribute__((aligned(16)));
static u8 rx_bufs[RX_DESCS][RX_BUF] __attribute__((aligned(16)));
static u8 tx_buf[RX_BUF] __attribute__((aligned(16)));
static int rx_head;

/* The card DMAs through machine addresses, but this task only ever sees
 * its own translation; sys_v2p is the bridge. These hold the answers. */
static u32 rx_ring_pa, tx_ring_pa, tx_buf_pa;
static u32 rx_buf_pa[RX_DESCS];

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

void say(const char *s) {
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
void say_ip(const char *pre, const u8 ip[4], const char *post) {
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
void say_mac(const u8 m[6]) {
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
void nic_tx(const u8 *frame, int len) {
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

/* The irq half of the service loop, opened up: sleep until the line
 * fires, clear the card's interrupt cause, sweep the rx ring, then
 * re-arm the pic line. The order is not negotiable: the card's irq
 * output is level-triggered through the pic, so reading ICR -- which
 * drops the line -- must precede the eoi, or the pic re-fires the same
 * level forever. The kernel leaves the eoi of a claimed line to its
 * owner for exactly this reason.
 * (Received frames keep the 4-byte FCS unless RCTL asks otherwise, so
 * rx len is a ceiling, never an exact size; the protocol parsers in
 * arp.c and ip.c trust the header fields instead.) */
/* The rx ring, drained: every descriptor the card has filled goes to
 * net_rx, then the tail is pushed so the card may reuse the slot. */
void nic_sweep(void) {
    while (rx_ring[rx_head].status & 1) {
        net_rx(rx_bufs[rx_head], rx_ring[rx_head].len);
        rx_ring[rx_head].status = 0;
        rx_head = (rx_head + 1) % RX_DESCS;
        wr(E1000_RDT, (rx_head + RX_DESCS - 1) % RX_DESCS);
    }
}

void nic_pump(void) {
    sys_irq_wait(1u << irq);
    (void)rd(E1000_ICR);                  /* quiet the card, line falls */
    nic_sweep();
    sys_irq_eoi(irq);                     /* now the pic may re-arm */
}

/* Read-and-clear the cause bits: the service loop's way of telling the
 * card's interrupt from the timer's without a full pump. */
u32 nic_icr(void) {
    return rd(E1000_ICR);
}

u32 nic_irq(void) {
    return (u32)irq;
}

void nic_bringup(void) {
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
