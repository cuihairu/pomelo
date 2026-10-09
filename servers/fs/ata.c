#include "../../kernel/types.h"
#include "../../kernel/kprintf.h"
#include "fs.h"

/* Primary ATA controller, LBA28, polled PIO. This is the only file in
 * the project that wrestles real hardware timing, and the kernel never
 * sees it: the fs task owns the disk, the way a user-space driver would.
 *
 * One failure mode dominates: the legacy ATA ports (0x1F0..) sit on the
 * PIIX3 ISA bridge of the QEMU `pc` machine. The `q35` machine wires its
 * disk through AHCI instead and leaves those ports floating, and a read
 * off a floating bus returns 0xFF -- whose BSY bit is set, so the naive
 * spin waits forever for a device that is not there. We detect that up
 * front and let the caller degrade instead of hanging the fs task. */

#define ATA_DATA 0x1F0
#define ATA_ERR  0x1F1
#define ATA_SECC 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_DRV  0x1F6
#define ATA_STAT 0x1F7
#define ATA_CMD  0x1F7

#define SR_BSY 0x80
#define SR_DRQ 0x08
#define SR_ERR 0x01

/* Every wait below naps with hlt between polls: a tight port-poll would
 * pin the CPU for the whole transfer -- and on an emulator, starve the
 * very host thread that has to complete the command. Politeness is not
 * optional in a microkernel, even in its one polling driver. */

/* The 0xFF signature: every line high means nobody is driving the bus.
 * Also catch ABRT-style errors so we do not wait on a failed command. */
static int ata_dead(void) {
    u8 st = inb(ATA_STAT);
    if (st == 0xFF) return 1;
    if (st & SR_ERR) return 1;
    return 0;
}

/* Device idle and ready to take a command. No DRQ here: before a
 * command there is nothing to transfer yet. */
static int ata_ready(void) {
    while (inb(ATA_STAT) & SR_BSY) {
        if (ata_dead()) return -1;
        intr_enable();
        halt();
        intr_disable();
    }
    intr_enable();
    return ata_dead() ? -1 : 0;
}

/* A command is running and its data is on the data port. Read the
 * status a few times first: the first read can catch a stale value
 * from before the command. */
static int ata_drq(void) {
    if (ata_ready() < 0) return -1;
    for (int i = 0; i < 4; i++) (void)inb(ATA_STAT);   /* ~400 ns settle */
    for (;;) {
        u8 st = inb(ATA_STAT);
        if (st & SR_ERR) return -1;
        if (st == 0xFF) return -1;
        if (st & SR_DRQ) break;
        intr_enable();
        halt();
        intr_disable();
    }
    intr_enable();
    return 0;
}

static int ata_start(u32 lba, u8 cmd) {
    if (ata_ready() < 0) return -1;
    outb(ATA_ERR, 0);
    outb(ATA_SECC, 1);                        /* one sector */
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
    outb(ATA_DRV, 0xE0 | ((lba >> 24) & 0x0F)); /* master, LBA28 top bits */
    outb(ATA_CMD, cmd);
    return 0;
}

int ata_read(u32 lba, void *buf) {
    if (ata_start(lba, 0x20) < 0) return -1;  /* READ SECTORS */
    if (ata_drq() < 0) return -1;
    insw(ATA_DATA, buf, SECTOR / 2);
    return 0;
}

int ata_write(u32 lba, const void *buf) {
    if (ata_start(lba, 0x30) < 0) return -1;  /* WRITE SECTORS */
    if (ata_drq() < 0) return -1;
    outsw(ATA_DATA, buf, SECTOR / 2);
    return ata_ready();                       /* wait out the flush */
}
