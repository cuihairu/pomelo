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

#define IRQ0 0   /* PIT */
#define IRQ1 1   /* keyboard */

void idt_init(void);
void pic_remap(void);
void pic_eoi(u32 irq);

void intr_dispatch(struct regs *r);
void irq_install(u32 irq, void (*fn)(void));
void irq_raise(u32 irq);   /* called from intr_dispatch */
void irq_wait(u32 irq);    /* block the current task until irq fires */

#endif
