#ifndef POMELO_SYSCALL_H
#define POMELO_SYSCALL_H

#include "../types.h"
#include "../ipc/ipc.h"
#include "../mm/frame.h"
#include "../sched/sched.h"

/* Well-known task ids, assigned in spawn order by boot/main.c. */
#define TID_FS     1
#define TID_TTY    2
#define TID_SHELL  3
#define TID_PROBE  4
#define TID_NET    5

enum {
    SYS_YIELD    = 0,   /* give up the rest of this time slice */
    SYS_SEND     = 1,   /* ebx=dst, ecx=msg* */
    SYS_RECV     = 2,   /* ebx=msg*, ecx=block */
    SYS_IRQ_WAIT = 3,   /* ebx=irq mask */
    SYS_REBOOT   = 4,
    SYS_WRITE    = 5,   /* ebx=buf, ecx=len: console output, kernel side */
    SYS_CLEAR    = 6,   /* clear the console */
    SYS_SPAWN    = 7,   /* ebx=name: start a registered program */
    SYS_PS       = 8,   /* ebx=buf, ecx=max: task-table snapshot */
    SYS_UPTIME   = 9,   /* seconds since boot: the heartbeat, read back */
    SYS_MEM      = 10,  /* ebx=&mem_info: the frame bitmap, counted */
    SYS_IRQ_ENABLE = 11, /* ebx=irq: open the pic line, park a wake stub */
    SYS_V2P      = 12,  /* ebx=va: virtual to physical, for DMA descriptors */
    SYS_MMIO     = 13,  /* ebx=pa: map one device page, returns its va */
    SYS_IRQ_EOI  = 14,  /* ebx=irq: re-arm the pic line this task owns */
};

struct regs;                       /* kernel/intr/intr.h */
void syscall_entry(struct regs *r);

/* User-side: enter the kernel through the int 0x80 gate only. */
static inline i32 gate(int n, int a, int b) {
    i32 ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}
static inline int  sys_send(int dst, struct msg *m) { return gate(SYS_SEND, dst, (int)m); }
static inline int  sys_recv(struct msg *m)          { return gate(SYS_RECV, (int)m, 1); }
static inline int  sys_try_recv(struct msg *m)      { return gate(SYS_RECV, (int)m, 0); }
static inline void sys_yield(void)                  { (void)gate(SYS_YIELD, 0, 0); }
static inline void sys_irq_wait(int irq)            { (void)gate(SYS_IRQ_WAIT, irq, 0); }
static inline void sys_reboot(void)                 { (void)gate(SYS_REBOOT, 0, 0); }
static inline void sys_write(const char *s, int n)  { (void)gate(SYS_WRITE, (int)s, n); }
static inline void sys_clear(void)                  { (void)gate(SYS_CLEAR, 0, 0); }
static inline int  sys_spawn(const char *name)      { return gate(SYS_SPAWN, (int)name, 0); }
static inline int  sys_ps(struct ps_entry *b, int n){ return gate(SYS_PS, (int)b, n); }
static inline u32  sys_uptime(void)                 { return gate(SYS_UPTIME, 0, 0); }
static inline void sys_mem(struct mem_info *m)      { (void)gate(SYS_MEM, (int)m, 0); }
static inline void sys_irq_enable(int irq)          { (void)gate(SYS_IRQ_ENABLE, irq, 0); }
static inline void sys_irq_eoi(int irq)             { (void)gate(SYS_IRQ_EOI, irq, 0); }
static inline u32  sys_v2p(u32 va)                  { return gate(SYS_V2P, (int)va, 0); }
static inline u32  sys_mmio(u32 pa)                 { return gate(SYS_MMIO, (int)pa, 0); }

/* sys_send reports a full queue instead of waiting; a caller that must
 * not lose the message retries -- a request dropped is a reply you
 * would wait for forever. The queue always drains, because receiver
 * main loops never block on a full queue, only on an empty one. */
static inline void sys_send_wait(int dst, struct msg *m) {
    while (sys_send(dst, m) < 0) sys_yield();
}

#endif
