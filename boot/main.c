#include "gdt.h"
#include "../kernel/char.h"
#include "../kernel/kprintf.h"
#include "../kernel/vga.h"
#include "../kernel/intr/intr.h"
#include "../kernel/mm/paging.h"
#include "../kernel/sched/sched.h"
#include "../kernel/syscall/syscall.h"

/* The user programs ride inside the kernel image as blobs; boot does
 * not start them so much as register them and spawn the opening cast
 * through the same door a shell command would use later. */
extern const u8 _binary_ufs_bin_start[],    _binary_ufs_bin_end[];
extern const u8 _binary_utty_bin_start[],   _binary_utty_bin_end[];
extern const u8 _binary_ushell_bin_start[], _binary_ushell_bin_end[];
extern const u8 _binary_uprobe_bin_start[], _binary_uprobe_bin_end[];

static void servers_start(void) {
    sched_init();
    prog_add("fs",    _binary_ufs_bin_start,    _binary_ufs_bin_end,
             EFLAGS_USER_IOPL);     /* the ata driver needs port rights */
    prog_add("tty",   _binary_utty_bin_start,   _binary_utty_bin_end,
             EFLAGS_USER);
    prog_add("shell", _binary_ushell_bin_start, _binary_ushell_bin_end,
             EFLAGS_USER);
    prog_add("probe", _binary_uprobe_bin_start, _binary_uprobe_bin_end,
             EFLAGS_USER);          /* born to crash, revivable by hand */

    sched_spawn("fs");              /* spawn order fixes the task ids */
    sched_spawn("tty");
    sched_spawn("shell");
    sched_spawn("probe");
}

void kmain(u32 magic, u32 info) {
    (void)info;
    vga_clear();
    kprintf("pomelo booting (multiboot magic=%x)\n", magic);

    gdt_init();                 /* segments first: IDT entries point at 0x08 */
    idt_init();                 /* then the interrupt gates */
    pic_remap();                /* IRQs to vectors 32..47 */
    pit_init(100);              /* 100 Hz: the scheduler's heartbeat */
    char_init();                /* console ISRs feed the tty by message */

    paging_init();              /* identity map on: addresses unchanged */
    kprintf("paging on: low 16m identity\n");

    servers_start();            /* fs, tty, shell, probe, all in ring 3 */

    intr_enable();              /* only now may interrupts fire */
    sched_enter(TID_FS);        /* kmain hands the stage to the services */
}
