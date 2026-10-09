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

/* First entry into a ring 3 task. The stack top is a full iret frame
 * (eip, cs, eflags, esp, ss) laid down by task_spawn_user; loading the
 * user data segments is the last thing kernel rings do on the way out. */
.globl ring3_entry
ring3_entry:
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    iret
