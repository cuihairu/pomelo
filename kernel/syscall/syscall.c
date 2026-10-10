#include "syscall.h"
#include "../char.h"
#include "../intr/intr.h"
#include "../mm/paging.h"
#include "../sched/sched.h"

/* Everything the kernel will ever do for a task, on one page. */

/* The net driver's wake stub: does nothing itself -- irq_raise does the
 * waking -- it only gives the line an owner so empty lines stay masked. */
void net_irq_stub(void) { }

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
    case SYS_WRITE:
        con_write((const char *)r->ebx, r->ecx);
        r->eax = 0;
        return;
    case SYS_CLEAR:
        con_clear();
        r->eax = 0;
        return;
    case SYS_SPAWN:
        r->eax = sched_spawn((const char *)r->ebx);
        return;
    case SYS_PS:
        r->eax = ps_snapshot((struct ps_entry *)r->ebx, r->ecx);
        return;
    case SYS_UPTIME:
        r->eax = sched_uptime();
        return;
    case SYS_MEM:
        frame_stats((struct mem_info *)r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_ENABLE:
        /* The pic mask and the idt are kernel territory; the driver that
         * owns the line asks here. The stub is empty by design: waking
         * the waiter is irq.c's job, and the driver clears the device's
         * own interrupt state once it runs. Claiming the line is what
         * moves its eoi from the kernel to the driver (sys_irq_eoi). */
        pic_irq_enable(r->ebx);
        irq_install(r->ebx, net_irq_stub);
        irq_claim(r->ebx);
        r->eax = 0;
        return;
    case SYS_IRQ_EOI:
        /* The other half of owning a line: the driver quieted its device,
         * now the pic may re-arm it. The kernel skipped the eoi on
         * delivery exactly so this could happen in the right order. */
        pic_eoi(r->ebx);
        r->eax = 0;
        return;
    case SYS_V2P:
        r->eax = v2p(r->ebx);
        return;
    case SYS_MMIO:
        r->eax = mmio_map(r->ebx);
        return;
    }
    r->eax = -1;                      /* unknown syscall number */
}
