#include "gdt.h"
#include "../kernel/types.h"

/* Flat memory model: every segment is base 0, limit 4G. We restate the
 * GDT rather than trusting whatever the bootloader left behind. Slots 3
 * and 4 are the ring 3 twins of the kernel segments -- same reach, lower
 * privilege, which is what makes the syscall gate the only door. */

struct seg {
    u16 limit_lo;
    u16 base_lo;
    u8  base_mid;
    u8  access;
    u8  limit_hi;    /* low nibble: limit bits 16-19; high: flags */
    u8  base_hi;
} __attribute__((packed));

static struct seg gdt[6] = {
    {0x0000, 0, 0, 0x00, 0x00, 0},   /* 0x00: null */
    {0xFFFF, 0, 0, 0x9A, 0xCF, 0},   /* 0x08: code, ring 0, 32-bit, 4G */
    {0xFFFF, 0, 0, 0x92, 0xCF, 0},   /* 0x10: data, ring 0, 32-bit, 4G */
    {0xFFFF, 0, 0, 0xFA, 0xCF, 0},   /* 0x18: code, ring 3 */
    {0xFFFF, 0, 0, 0xF2, 0xCF, 0},   /* 0x20: data, ring 3 */
    {0x0067, 0, 0, 0x89, 0x40, 0},   /* 0x28: TSS, filled in below */
};

/* The TSS holds one thing we care about: esp0, the kernel stack the CPU
 * switches to when ring 3 takes a gate. It is rewritten on every switch
 * to a user task -- one task shares the register file at a time, so one
 * esp0 slot is enough. */
static struct tss {
    u32 prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
    u32 eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} __attribute__((packed)) tss;

void tss_esp0_set(u32 top) {
    tss.esp0 = top;
}

static struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) gdtr = { sizeof(gdt) - 1, (u32)gdt };

void gdt_init(void) {
    u32 tss_base = (u32)&tss;
    gdt[5].base_lo  = tss_base & 0xFFFF;
    gdt[5].base_mid = (tss_base >> 16) & 0xFF;
    gdt[5].base_hi  = (tss_base >> 24) & 0xFF;
    tss.ss0 = 0x10;

    __asm__ volatile("lgdt %0" :: "m"(gdtr));
    __asm__ volatile(
        "ljmp $0x08, $1f\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        ::: "ax");
    __asm__ volatile("ltr %%ax" :: "a"(0x28));
}
