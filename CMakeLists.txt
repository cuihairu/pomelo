cmake_minimum_required(VERSION 3.18)
project(pomelo C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# kernel: i386 freestanding ELF, loaded by `qemu-system-i386 -kernel`.
# No libc, no host runtime: the whole point is "compile what you wrote".
# ---------------------------------------------------------------------------
set(KSRC
    boot/boot.S boot/gdt.c boot/main.c
    kernel/kprintf.c kernel/string.c kernel/vga.c
    kernel/intr/idt.c kernel/intr/intr.c kernel/intr/irq.c
    kernel/intr/stubs.S
    kernel/mm/frame.c kernel/mm/paging.c
    kernel/sched/sched.c kernel/sched/switch.S
    kernel/ipc/ipc.c
    kernel/syscall/syscall.c
    servers/fs/ata.c servers/fs/fs.c servers/fs/fsrw.c
    servers/tty/console.c servers/tty/kbd.c servers/tty/tty.c
    apps/shell/shell.c apps/shell/fsio.c apps/shell/cmd_fs.c
    apps/shell/cmd_help.c apps/shell/cmd_sys.c)

add_executable(kernel ${KSRC})
target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(kernel PRIVATE
    -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin
    -nostdlib -Wall -Wextra -O2 -g)
target_link_options(kernel PRIVATE
    -m32 -nostdlib -no-pie -static -Wl,--build-id=none
    -Wl,-T,${CMAKE_SOURCE_DIR}/boot/kernel.ld)

# ---------------------------------------------------------------------------
# host tool: mkfs builds pomelo.img (superblock + inode table + seeds).
# ---------------------------------------------------------------------------
add_executable(mkfs tools/mkfs/mkfs.c)
target_compile_options(mkfs PRIVATE -O2 -Wall -Wextra)

set(IMG ${CMAKE_BINARY_DIR}/pomelo.img)
add_custom_command(OUTPUT ${IMG}
    COMMAND $<TARGET_FILE:mkfs> ${IMG} ${CMAKE_SOURCE_DIR}/tools/mkfs/seed
    DEPENDS mkfs)
add_custom_target(image ALL DEPENDS ${IMG})

# bootable iso (grub multiboot); optional, needs grub-mkrescue + xorriso
find_program(GRUB_MKRESCUE grub-mkrescue)
if(GRUB_MKRESCUE)
    set(ISO ${CMAKE_BINARY_DIR}/pomelo.iso)
    add_custom_command(OUTPUT ${ISO}
        COMMAND sh ${CMAKE_SOURCE_DIR}/tools/mkiso.sh
                $<TARGET_FILE:kernel> ${ISO}
        DEPENDS kernel)
    add_custom_target(iso DEPENDS ${ISO})
endif()

# ---------------------------------------------------------------------------
# run & smoke test: boot in QEMU, drive the shell over the serial port.
# ---------------------------------------------------------------------------
find_program(QEMU qemu-system-i386 REQUIRED)
add_custom_target(run
    COMMAND ${QEMU} -kernel $<TARGET_FILE:kernel> -hda ${IMG} -serial stdio
    DEPENDS kernel image)

enable_testing()
set(KERNEL_ELF ${CMAKE_BINARY_DIR}/kernel)
add_test(NAME smoke
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke.log)
