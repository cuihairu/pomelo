#include "../kernel/types.h"

/* Flat memory model: every segment is base 0, limit 4G. We restate the
 * GDT rather than trusting whatever the bootloader left behind. */

struct seg {
    u16 limit_lo;
    u16 base_lo;
    u8  base_mid;
    u8  access;
    u8  limit_hi;    /* low nibble: limit bits 16-19; high: flags */
    u8  base_hi;
} __attribute__((packed));

static struct seg gdt[3] = {
    {0x0000, 0, 0, 0x00, 0x00, 0},   /* 0x00: null */
    {0xFFFF, 0, 0, 0x9A, 0xCF, 0},   /* 0x08: code, ring 0, 32-bit, 4G */
    {0xFFFF, 0, 0, 0x92, 0xCF, 0},   /* 0x10: data, ring 0, 32-bit, 4G */
};

static struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) gdtr = { sizeof(gdt) - 1, (u32)gdt };

void gdt_init(void) {
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
}
