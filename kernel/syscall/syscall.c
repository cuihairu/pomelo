#include "syscall.h"
#include "../intr/intr.h"
#include "../sched/sched.h"

/* Everything the kernel will ever do for a task, on one page. */

static void reboot_now(void) {
    intr_disable();
    for (;;) {                 /* pulse the 8042 reset line; loops if not */
        outb(0x64, 0xFE);
        halt();
    }
}

void syscall_entry(struct regs *r) {
    switch (r->eax) {
    case SYS_YIELD:
        sched_tick();                 /* round-robin: hand over the CPU */
        r->eax = 0;
        return;
    case SYS_SEND:
        r->eax = ipc_send(r->ebx, (struct msg *)r->ecx);
        return;
    case SYS_RECV:
        r->eax = ipc_recv((struct msg *)r->ebx, r->ecx);
        return;
    case SYS_IRQ_WAIT:
        irq_wait(r->ebx);
        r->eax = 0;
        return;
    case SYS_REBOOT:
        reboot_now();
        return;
    }
    r->eax = -1;                      /* unknown syscall number */
}
