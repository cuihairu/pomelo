#ifndef POMELO_TYPES_H
#define POMELO_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed int         i32;

#define NULL ((void *)0)

/* I/O ports: the only way a CPU talks to old ISA devices. */
static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* 16-bit string read/write between a port and memory: ATA driver.
 * insw fills %es:(%edi), outsw drains %ds:(%esi); both advance their
 * pointer and counter, so all three registers are in-out. */
static inline void insw(u16 port, void *addr, u32 count) {
    __asm__ volatile("rep insw"
        : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(u16 port, const void *addr, u32 count) {
    __asm__ volatile("rep outsw"
        : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

#include "string.h"

/* Stop the CPU until the next interrupt. */
static inline void halt(void) {
    __asm__ volatile("hlt");
}

static inline void intr_enable(void)  { __asm__ volatile("sti"); }
static inline void intr_disable(void) { __asm__ volatile("cli"); }

#endif
