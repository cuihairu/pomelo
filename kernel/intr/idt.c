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

/* Defined in stubs.S: one wrapper per hardware vector, listed in vector
 * order, plus the paths below. */
extern const u32 irq_stub_table[16];
void intr_stub_exc(void);
void intr_stub_13(void);      /* GPF: ring 3 offender or kernel bug */
void intr_stub_14(void);      /* page fault: same split */
void intr_stub_128(void);     /* the syscall gate */

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        idt_set(i, 0, 0, 0);          /* not-present by default */

    /* CPU exceptions share one stub: we do not decode them, we stop.
     * 13 and 14 are the exception: they carry an error code and are the
     * ones a ring 3 task can trigger, so they get their own path. */
    for (int i = 0; i < 32; i++)
        if (i != 13 && i != 14)
            idt_set(i, (u32)intr_stub_exc, 0x08, 0x8E);
    idt_set(13, (u32)intr_stub_13, 0x08, 0x8E);
    idt_set(14, (u32)intr_stub_14, 0x08, 0x8E);

    /* Hardware IRQs live at 32..47 after the PIC remap. Every line gets
     * the same gate: the ones owned at boot (PIT, kbd, com1) and any a
     * driver opens later via sys_irq_enable ride the same path, and an
     * unclaimed interrupt is acked and parked instead of #GP-ing the
     * machine. */
    for (int i = 0; i < 16; i++)
        idt_set(32 + i, irq_stub_table[i], 0x08, 0x8E);

    /* The syscall gate. DPL 3: user code may raise it, hardware IRQs may
     * not be raised by anyone. */
    idt_set(0x80, (u32)intr_stub_128, 0x08, 0xEE);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (u32)idt;
    idt_load();
}
