/* Multiboot v1 header + 32-bit entry. QEMU's `-kernel` acts as the
 * bootloader: it loads this ELF, switches to protected mode, puts the
 * magic in %eax and the info pointer in %ebx, and jumps to _start. */

.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x0                 /* we need nothing extra from the loader */
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECK                 /* sum of the three longs must be 0 */

.section .bss
.align 16
boot_stack_bottom:
    .skip 16384                    /* C needs a stack before it needs anything */
boot_stack_top:

.section .text
.globl _start
_start:
    movl $boot_stack_top, %esp
    pushl %ebx                     /* multiboot info pointer */
    pushl %eax                     /* magic, for kmain to verify */
    call  kmain
1:  cli
    hlt
    jmp 1b                         /* kmain must never return */
