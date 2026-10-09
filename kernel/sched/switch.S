/* switch_to(u32 *save_esp, u32 new_esp): the context switch. Saves the
 * callee-saved registers on the current stack, swaps stacks, restores.
 * A timer interrupt in the middle of a stack swap would corrupt both
 * tasks, so the swap runs with interrupts off. */

.section .text

.globl switch_to
switch_to:
    cli
    movl  4(%esp), %eax         # &old->esp
    movl  8(%esp), %edx         # new->esp
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %ebx
    movl  %esp, (%eax)          # old task resumes exactly here
    movl  %edx, %esp            # become the new task
    popl  %ebx
    popl  %esi
    popl  %edi
    popl  %ebp
    ret                         # pops the new task's resume address

/* First entry into a fresh task: switch_to rets here, we switch
 * interrupts on, then ret again into the task's real entry. */
.globl task_trampoline
task_trampoline:
    sti
    ret
