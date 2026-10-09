#ifndef POMELO_SYSCALL_H
#define POMELO_SYSCALL_H

#include "../types.h"
#include "../ipc/ipc.h"

/* Well-known task ids, assigned in spawn order by boot/main.c. */
#define TID_FS    1
#define TID_TTY   2
#define TID_SHELL 3

enum {
    SYS_YIELD    = 0,   /* give up the rest of this time slice */
    SYS_SEND     = 1,   /* ebx=dst, ecx=msg* */
    SYS_RECV     = 2,   /* ebx=msg*, ecx=block */
    SYS_IRQ_WAIT = 3,   /* ebx=irq mask */
    SYS_REBOOT   = 4,
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

#endif
