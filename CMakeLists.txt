cmake_minimum_required(VERSION 3.18)
project(pomelo C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# user programs: linked at 0x40000000 (their own address space), stripped
# to flat binaries and embedded in the kernel as blobs.
# ---------------------------------------------------------------------------
function(pomelo_user name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(${name} PRIVATE
        -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin
        -fno-asynchronous-unwind-tables -fcf-protection=none
        -O2 -g -Wall -Wextra)
    target_link_options(${name} PRIVATE
        -m32 -nostdlib -no-pie -static -Wl,--build-id=none
        -T ${CMAKE_SOURCE_DIR}/tools/user.ld -e 0x40000000)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${name}.bin
        COMMAND objcopy -O binary $<TARGET_FILE:${name}>
                ${CMAKE_BINARY_DIR}/${name}.bin
        DEPENDS ${name})
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${name}_blob.o
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}
                ld -m elf_i386 -r -b binary -o ${name}_blob.o ${name}.bin
        DEPENDS ${CMAKE_BINARY_DIR}/${name}.bin)
    add_custom_target(${name}_blob DEPENDS ${CMAKE_BINARY_DIR}/${name}_blob.o)
endfunction()

pomelo_user(ushell
    apps/shell/shell.c apps/shell/fsio.c apps/shell/cmd_fs.c
    apps/shell/cmd_help.c apps/shell/cmd_sys.c
    kernel/string.c)
pomelo_user(uprobe apps/probe/probe.c kernel/string.c)

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
    ${CMAKE_BINARY_DIR}/ushell_blob.o
    ${CMAKE_BINARY_DIR}/uprobe_blob.o)

set_source_files_properties(${CMAKE_BINARY_DIR}/ushell_blob.o
                            ${CMAKE_BINARY_DIR}/uprobe_blob.o
                            PROPERTIES GENERATED TRUE)

add_executable(kernel ${KSRC})
add_dependencies(kernel ushell_blob uprobe_blob)
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
