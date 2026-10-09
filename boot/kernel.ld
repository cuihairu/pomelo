/* Link the kernel at 1M, multiboot header first so `-kernel` finds it. */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text : ALIGN(4) {
        KEEP(*(.multiboot))
        *(.text*)
    }

    .rodata : ALIGN(4) {
        *(.rodata*)
    }

    .data : ALIGN(4) {
        *(.data*)
    }

    .bss : ALIGN(4) {
        *(COMMON)
        *(.bss*)
    }

    kernel_end = .;
}
