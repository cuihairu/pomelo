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

/* A pointer that crossed the gate is a trust boundary: ring 3 may hand
 * us anything. Validate before dereferencing -- the range must sit in the
 * user region and every page it touches must be mapped in the calling
 * task's address space, or a bad pointer would fault the kernel (panic)
 * instead of the task that passed it. v2p walks the caller's own map. */
static int user_ok(const void *p, u32 len) {
    u32 a = (u32)p, b = a + len;
    if (b < a) return 0;                       /* wrapped around */
    if (a < USER_BASE || b > USER_STACK_TOP) return 0;
    while (a < b) {
        if (!v2p(a)) return 0;                 /* a hole: page not mapped */
        a = (a & ~0xFFFu) + 4096;
    }
    return 1;
}

void syscall_entry(struct regs *r) {
    switch (r->eax) {
    case SYS_YIELD:
        sched_tick();                 /* round-robin: hand over the CPU */
        r->eax = 0;
        return;
    case SYS_SEND:
        if (!user_ok((void *)r->ecx, sizeof(struct msg))) { r->eax = -1; return; }
        r->eax = ipc_send(r->ebx, (struct msg *)r->ecx);
        return;
    case SYS_RECV:
        if (!user_ok((void *)r->ebx, sizeof(struct msg))) { r->eax = -1; return; }
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
        /* ecx is unsigned here; a huge length fails the user_ok range
         * check on the wrap instead of overflowing the console loop. */
        if (!user_ok((const void *)r->ebx, r->ecx)) { r->eax = -1; return; }
        con_write((const char *)r->ebx, r->ecx);
        r->eax = 0;
        return;
    case SYS_CLEAR:
        con_clear();
        r->eax = 0;
        return;
    case SYS_SPAWN:
        /* sched_spawn reads at most PROG_NAME bytes of the name. */
        if (!user_ok((const void *)r->ebx, PROG_NAME)) { r->eax = -1; return; }
        r->eax = sched_spawn((const char *)r->ebx);
        return;
    case SYS_PS: {
        /* ps_snapshot writes at most NTASK-1 rows, whatever max says. */
        u32 want = (u32)r->ecx;
        if (want > NTASK) want = NTASK;
        if (!user_ok((void *)r->ebx, want * sizeof(struct ps_entry))) {
            r->eax = -1;
            return;
        }
        r->eax = ps_snapshot((struct ps_entry *)r->ebx, r->ecx);
        return;
    }
    case SYS_UPTIME:
        r->eax = sched_uptime();
        return;
    case SYS_MEM:
        if (!user_ok((void *)r->ebx, sizeof(struct mem_info))) {
            r->eax = -1;
            return;
        }
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
