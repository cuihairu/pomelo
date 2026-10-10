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

void idt_init(void);
void pic_remap(void);
void pic_eoi(u32 irq);
void pic_irq_enable(u32 irq);
void pit_init(u32 hz);

void intr_dispatch(struct regs *r);
void panic(const char *why, struct regs *r);
void irq_install(u32 irq, void (*fn)(void));
void irq_dispatch(u32 irq);/* run a line's handler, if owned */
int  irq_owned(u32 irq);   /* is this line claimed by a ring 3 driver? */
void irq_claim(u32 irq);   /* mark a line as a ring 3 driver's own */
void irq_raise(u32 irq);   /* called from intr_dispatch */
void irq_wait(u32 mask);   /* block until any irq in the mask fires */

#endif
