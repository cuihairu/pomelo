# 02 · Startup

Corresponding code: `boot/`

## What happens after power-on

After the CPU resets it stops in real mode; the BIOS finishes its self-check and hands control to the bootloader. We do not want to write our own bootloader, and do not want to make an ISO, so we borrow one convention: **Multiboot**. QEMU's `-kernel` can recognize an ELF file carrying a Multiboot header and acts as the bootloader itself: it loads the kernel into memory, switches to protected mode, then jumps to the entry we specify.

This convention needs only 12 bytes at the head of the kernel (magic, flags, checksum, one long word each) plus 4-byte alignment. Look at `boot/boot.S`:

```asm
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x0            # the kernel is loaded by the bootloader, no extra memory info requested
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECK
```

`CHECK` is the two's-complement sum of the two above; the sum must be 0 — that is the integrity check Multiboot requires. `boot/kernel.ld` places this section at the very front, guaranteeing the magic number lands at the start of the file.

## Jumping from assembly into C

Once the bootloader has switched into protected mode, there is no stack to count on — you have to set one up yourself:

```asm
_start:
    movl $boot_stack_top, %esp  # set up the stack, otherwise the first returning C function crashes
    pushl %ebx                  # pointer to the Multiboot info structure, handed to kmain
    pushl %eax                  # the magic, handed to kmain for verification
    call  kmain
1:  cli
    hlt
    jmp 1b                      # the kernel entry should not return; if it really does, stop
```

The stack space is one static array opened up in `.bss` — there is no memory management yet at this point, so use static memory wherever static memory works.

## The first line of C

`kmain` in `boot/main.c` is the gathering point of the whole boot chain, and the order cannot be shuffled:

```c
void kmain(u32 magic, u32 info) {
    (void)info;
    vga_clear();
    kprintf("pomelo booting (multiboot magic=%x)\n", magic);

    gdt_init();                 /* segments first: IDT entries point at 0x08 */
    idt_init();                 /* then the interrupt gates */
    pic_remap();                /* IRQs to vectors 32..47 */
    pit_init(HZ);               /* 100 Hz: the scheduler's heartbeat */
    char_init();                /* console ISRs feed the tty by message */
    mouse_init();               /* the other 8042 device: probe, then irq12 */

    paging_init();              /* identity map on: addresses unchanged */
    kprintf("paging on: low 16m identity\n");

    servers_start();            /* fs, tty, shell, probe, net, netsh: ring 3 */

    intr_enable();              /* only now may interrupts fire */
    sched_enter(TID_FS);        /* kmain hands the stage to the services */
}
```

Notice this final jump: after `kmain` has built the stage it hands the CPU to the first task, and its own stack is recorded as "task 0", staying forever inside `switch_to`. From then on, all work happens in the tasks created by `servers_start`. That is exactly the posture of a microkernel — the kernel is not the protagonist, it is just the stage.

## A pitfall of ordering

You must do `idt_init` before `intr_enable`. If interrupts are enabled while the interrupt gates are not yet installed, the first clock tick jumps to an empty vector and the machine triple-faults and resets on the spot. The most common beginner symptom of "reboots the moment it powers on" is mostly this one.

Paging follows the same rule: `paging_init` before `servers_start` — page tables built, PG bit flipped on, and only then are tasks qualified to come on stage. Do it the other way around, building the page tables after the tasks are already running, and the first task steps straight into an address that is not mapped yet.

## Files in this chapter

```
boot/boot.S          Multiboot header + _start
boot/main.c          kmain: initializes hardware, starts the services, enables interrupts
boot/kernel.ld       linker script, magic placed at the start of the file
boot/gdt.c/.h        global descriptor table: kernel segments and TSS (Chapter 14)
kernel/kprintf.c/.h  the kernel-side printf: kmain's mouth
kernel/types.h       the basic types like u8/u32
```

The next chapter: [Interrupt dispatch](/guide/interrupts).
