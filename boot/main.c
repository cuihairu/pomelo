#include "../kernel/types.h"
#include "../kernel/kprintf.h"
#include "../kernel/vga.h"
#include "../kernel/intr/intr.h"
#include "../kernel/mm/paging.h"
#include "../kernel/sched/sched.h"
#include "../kernel/syscall/syscall.h"

/* bring-up lives in boot/, entries live with their services */
void gdt_init(void);
void fs_main(void);
void tty_main(void);
void shell_main(void);

static void servers_start(void) {
    sched_init();
    task_spawn(fs_main);        /* -> TID_FS */
    task_spawn(tty_main);       /* -> TID_TTY */
    task_spawn(shell_main);     /* -> TID_SHELL */
}

void kmain(u32 magic, u32 info) {
    (void)info;
    vga_clear();
    kprintf("pomelo booting (multiboot magic=%x)\n", magic);

    gdt_init();                 /* segments first: IDT entries point at 0x08 */
    idt_init();                 /* then the interrupt gates */
    pic_remap();                /* IRQs to vectors 32..47 */
    pit_init(100);              /* 100 Hz: the scheduler's heartbeat */

    paging_init();              /* identity map on: addresses unchanged */
    kprintf("paging on: low 16m identity\n");

    servers_start();            /* fs, tty, shell as runnable tasks */

    intr_enable();              /* only now may interrupts fire */
    sched_enter(TID_FS);        /* kmain hands the stage to the services */
}
