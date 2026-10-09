#include "gdt.h"
#include "../kernel/char.h"
#include "../kernel/kprintf.h"
#include "../kernel/vga.h"
#include "../kernel/intr/intr.h"
#include "../kernel/mm/paging.h"
#include "../kernel/sched/sched.h"
#include "../kernel/syscall/syscall.h"

/* The user programs ride inside the kernel image as blobs and come to
 * life in their own address spaces. The first four bytes of each blob
 * are the entry point (see tools/user.ld and the .hdr section). */
extern const u8 _binary_ufs_bin_start[],    _binary_ufs_bin_end[];
extern const u8 _binary_utty_bin_start[],   _binary_utty_bin_end[];
extern const u8 _binary_ushell_bin_start[], _binary_ushell_bin_end[];
extern const u8 _binary_uprobe_bin_start[], _binary_uprobe_bin_end[];

static void user_start(const u8 *begin, const u8 *end, u32 flags) {
    u32 dir = pdir_user_new((u32)begin, (u32)(end - begin));
    if (dir) task_spawn_user(dir, *(const u32 *)begin, USER_STACK_TOP, flags);
    else kprintf("user program dropped: no frames\n");
}

static void servers_start(void) {
    sched_init();
    user_start(_binary_ufs_bin_start, _binary_ufs_bin_end,
               EFLAGS_USER_IOPL);   /* -> TID_FS: ata ports at ring 3 */
    user_start(_binary_utty_bin_start, _binary_utty_bin_end,
               EFLAGS_USER);        /* -> TID_TTY */
    user_start(_binary_ushell_bin_start, _binary_ushell_bin_end,
               EFLAGS_USER);        /* -> TID_SHELL */
    user_start(_binary_uprobe_bin_start, _binary_uprobe_bin_end,
               EFLAGS_USER);        /* -> TID_PROBE, born to crash */
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
