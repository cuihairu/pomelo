/* Interrupt entry stubs. Each pushes a fake error code and its vector
 * number, then joins the common path. intr_common builds the struct regs
 * frame that intr_dispatch() reads. Field order must match intr.h. */

.macro STUB vec
    .globl intr_stub_\vec
intr_stub_\vec:
    pushl $0
    pushl $\vec
    jmp   intr_common
.endm

.section .text

STUB 32          /* IRQ_PIT: timer tick */
STUB 33          /* IRQ_KBD: PS/2 keyboard */
STUB 36          /* IRQ_COM1: serial receive */
STUB 128         /* the syscall gate */

/* All CPU exceptions share one stub: we do not decode them, we stop. */
.globl intr_stub_exc
intr_stub_exc:
    pushl $0
    pushl $0xDEAD
    jmp   intr_common

/* 13 (general protection) and 14 (page fault) push a real error code
 * themselves -- no fake here -- because the fault path needs to tell a
 * ring 3 offender (kill the task) from a kernel one (panic). */
.globl intr_stub_13
intr_stub_13:
    pushl $13
    jmp   intr_common

.globl intr_stub_14
intr_stub_14:
    pushl $14
    jmp   intr_common

.globl intr_common
intr_common:
    pusha                       /* edi, esi, ebp, esp, ebx, edx, ecx, eax */
    pushl %ds
    pushl %esp                  /* arg: struct regs * */
    call  intr_dispatch
    addl  $4, %esp
    popl  %ds
    popa
    addl  $8, %esp              /* drop int_no + err_code */
    iret

/* idt_load(): loads the IDT pointer prepared by idt.c. */
.globl idt_load
idt_load:
    movl  $idt_ptr, %eax
    lidt  (%eax)
    ret
