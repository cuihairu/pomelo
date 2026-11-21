#ifndef POMELO_INTR_H
#define POMELO_INTR_H

#include "../types.h"

/* Register snapshot pushed by kernel/intr/stubs.S, in push order read
 * backwards by dispatch. Field order must match the assembly. */
struct regs {
    u32 ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by stub */
    u32 int_no, err_code;                        /* pushed by stub */
    u32 eip, cs, eflags;                         /* pushed by CPU */
};

#define IRQ_PIT   0   /* timer */
#define IRQ_KBD   1   /* PS/2 keyboard */
#define IRQ_COM1  4   /* serial port receive */
#define IRQ_MOUSE 12  /* PS/2 mouse, the 8042's aux channel */

/* The lines the kernel keeps for itself: the timer that drives the
 * scheduler, the keyboard and serial isrs, the mouse -- and IRQ2, which
 * is not a device at all but the cascade every slave line arrives on.
 * A ring 3 driver must never claim one: the clock line claimed would
 * strand its eoi (the kernel would wait for a sys_irq_eoi that can
 * never come from interrupt context) and the machine would stop. This
 * mask is also what pic_remap opens at boot, so the boot mask and the
 * claim rule cannot drift apart. */
#define IRQ_KERNEL_MASK ((1u << IRQ_PIT) | (1u << IRQ_KBD) | (1u << 2) | \
                          (1u << IRQ_COM1) | (1u << IRQ_MOUSE))

/* The irq gate: only the two 8259s' sixteen lines exist. irq.c indexes
 * its handler/waiter tables by the line number, so anything past 15
 * would shift the masks past the table and poke pic ports that belong
 * to nobody. Pure, so the unit tests share the kernel's exact rule. */
#define NIRQ 16
static inline int irq_no_ok(u32 irq) { return irq < NIRQ; }

/* Is this a line the kernel itself services? A driver may open only a
 * line this returns 0 for. Pure for the same reason as irq_no_ok. */
static inline int irq_kernel(u32 irq) {
    return irq < NIRQ && ((IRQ_KERNEL_MASK >> irq) & 1u);
}

void idt_init(void);
void pic_remap(void);
void pic_eoi(u32 irq);
void pic_irq_enable(u32 irq);
void pic_irq_disable(u32 irq);
void pic_eoi_specific(u32 irq);
void pit_init(u32 hz);

void intr_dispatch(struct regs *r);
void panic(const char *why, struct regs *r);
void irq_install(u32 irq, void (*fn)(void));
void irq_dispatch(u32 irq);/* run a line's handler, if owned */
int  irq_owned(u32 irq);   /* is this line claimed by a ring 3 driver? */
int  irq_owned_by(u32 irq, int seat); /* ...and is that driver this seat? */
int  irq_line_of(int seat);     /* the line this seat claims, or -1 */
int  irq_claim(u32 irq);   /* take a line, exclusively; 0 ok, -1 refused */
void irq_release(int tid); /* a dead task's lines go back to the kernel */
void irq_raise(u32 irq);   /* called from intr_dispatch */
void irq_wait(u32 mask);   /* block until any irq in the mask fires */

#endif
