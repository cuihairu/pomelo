#include "intr.h"

/* The Interrupt Descriptor Table: 256 gates. CPU vector n reads entry n. */

struct idt_entry {
    u16 offset_lo;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 offset_hi;
} __attribute__((packed));

static struct idt_entry idt[256];

/* Referenced by name from stubs.S. */
struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt_ptr;

/* Called from assembly stubs; never inlined so each stub gets its own. */
void idt_load(void);

void idt_set(int n, u32 handler, u16 sel, u8 flags) {
    idt[n].offset_lo = handler & 0xffff;
    idt[n].selector  = sel;
    idt[n].zero      = 0;
    idt[n].flags     = flags;
    idt[n].offset_hi = handler >> 16;
}

/* Defined in stubs.S: one tiny wrapper per vector we actually use. */
void intr_stub_exc(void);
void intr_stub_32(void);      /* IRQ_PIT */
void intr_stub_33(void);      /* IRQ_KBD */
void intr_stub_36(void);      /* IRQ_COM1 */
void intr_stub_128(void);     /* the syscall gate */

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        idt_set(i, 0, 0, 0);          /* not-present by default */

    /* CPU exceptions share one stub: we do not decode them, we stop. */
    for (int i = 0; i < 32; i++)
        idt_set(i, (u32)intr_stub_exc, 0x08, 0x8E);

    /* Hardware IRQs live at 32..47 after the PIC remap. */
    idt_set(32 + IRQ_PIT,  (u32)intr_stub_32,  0x08, 0x8E);
    idt_set(32 + IRQ_KBD,  (u32)intr_stub_33,  0x08, 0x8E);
    idt_set(32 + IRQ_COM1, (u32)intr_stub_36,  0x08, 0x8E);

    /* The syscall gate. */
    idt_set(0x80, (u32)intr_stub_128, 0x08, 0x8E);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (u32)idt;
    idt_load();
}
