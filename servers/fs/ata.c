#include "../../kernel/types.h"
#include "fs.h"

/* Primary ATA controller, LBA28, polled PIO. This is the only file in
 * the project that wrestles real hardware timing, and the kernel never
 * sees it: the fs task owns the disk, the way a user-space driver would. */

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

/* Every wait below naps with hlt between polls: a tight port-poll would
 * pin the CPU for the whole transfer -- and on an emulator, starve the
 * very host thread that has to complete the command. Politeness is not
 * optional in a microkernel, even in its one polling driver. */

/* Device idle and ready to take a command. No DRQ here: before a
 * command there is nothing to transfer yet. */
static void ata_ready(void) {
    while (inb(ATA_STAT) & SR_BSY) {
        intr_enable();
        halt();
        intr_disable();
    }
    intr_enable();
}

/* A command is running and its data is on the data port. Read the
 * status a few times first: the first read can catch a stale value
 * from before the command. */
static void ata_drq(void) {
    ata_ready();
    for (int i = 0; i < 4; i++) (void)inb(ATA_STAT);   /* ~400 ns settle */
    while (!(inb(ATA_STAT) & SR_DRQ)) {
        intr_enable();
        halt();
        intr_disable();
    }
    intr_enable();
}

static void ata_start(u32 lba, u8 cmd) {
    ata_ready();
    outb(ATA_ERR, 0);
    outb(ATA_SECC, 1);                        /* one sector */
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
    outb(ATA_DRV, 0xE0 | ((lba >> 24) & 0x0F)); /* master, LBA28 top bits */
    outb(ATA_CMD, cmd);
}

int ata_read(u32 lba, void *buf) {
    ata_start(lba, 0x20);                     /* READ SECTORS */
    ata_drq();
    insw(ATA_DATA, buf, SECTOR / 2);
    return 0;
}

int ata_write(u32 lba, const void *buf) {
    ata_start(lba, 0x30);                     /* WRITE SECTORS */
    ata_drq();
    outsw(ATA_DATA, buf, SECTOR / 2);
    ata_ready();                              /* wait out the flush */
    return 0;
}
