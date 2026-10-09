/* User programs link at 0x40000000, in their own address space. The
 * .hdr section rides first and carries the entry point as a plain
 * 32-bit value: the kernel reads the first four bytes of the embedded
 * blob to learn where to jump. .bss is folded into .data so objcopy's
 * flat binary contains every byte the task will touch. */
OUTPUT_FORMAT(elf32-i386)
SECTIONS
{
    . = 0x40000000;
    .text : {
        KEEP(*(.hdr))
        *(.text*)
        *(.rodata*)
    }
    .data : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
}
