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

/* Every hardware line lands on the same tiny wrapper, one per vector:
 * the pic was remapped to 32..47 and any line a driver opens must have
 * a gate, claimed or not. Whether the line has an owner is irq.c's
 * business -- an unclaimed interrupt is acked and parked. */
.irp v, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
STUB \v
.endr

/* idt_init walks this to wire all sixteen vectors at once. */
.section .data
.globl irq_stub_table
irq_stub_table:
.irp v, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .long intr_stub_\v
.endr

.section .text
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
    cli                        /* the syscall gate is a trap gate: it leaves
                                * eflags.if open, so a tick could land in the
                                * few instructions between dispatch and popa.
                                * That tick would push its frame into the
                                * register spill popa is about to read, and
                                * this task would come back owning a register
                                * file that no longer belongs to it -- enough
                                * to make the final iret walk a wrong stack
                                * and raise a protection fault at the iret.
                                * Everything below walks the frame it is
                                * about to pop, so it stays atomic. Hardware
                                * lines already arrive with if=0, and the
                                * iret restores eflags from the frame, so a
                                * trap-gate syscall still resumes with
                                * interrupts on */
    pusha                       /* edi, esi, ebp, esp, ebx, edx, ecx, eax */
    pushl %ds
    pushl %esp                  /* arg: struct regs * */
    call  intr_dispatch
    addl  $4, %esp
    popl  %ds
    popa
    addl  $8, %esp              /* drop int_no + err_code */
    /* frame sentinel: the iret must pop a cs this kernel issues (0x08
     * kernel, 0x1b user). cs sits at 4(%esp) on either kind of frame:
     * the cpu pushes ss, esp, eflags, cs, eip across a privilege change
     * and eflags, cs, eip otherwise, so eip ends up lowest and cs is
     * always eight bytes below eip's neighbour. Anything else here means
     * some earlier write reached this frame. */
    cmpl  $0x08, 4(%esp)
    je    1f
    cmpl  $0x1b, 4(%esp)
    je    1f
    pushl %esp
    call  torn_frame            /* C: prints the raw frame, then stops */
1:  iret

/* idt_load(): loads the IDT pointer prepared by idt.c. */
.globl idt_load
idt_load:
    movl  $idt_ptr, %eax
    lidt  (%eax)
    ret
