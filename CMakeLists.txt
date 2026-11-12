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
    apps/shell/cmd_help.c apps/shell/cmd_sys.c apps/shell/cmd_proc.c
    apps/shell/cmd_echo.c
    kernel/string.c)
pomelo_user(uprobe apps/probe/probe.c kernel/string.c)

# the services themselves are user programs too: each runs in its own
# address space, entered exactly like shell and probe
pomelo_user(ufs  servers/fs/fs.c servers/fs/fsrw.c servers/fs/ata.c
    kernel/string.c)
pomelo_user(utty servers/tty/tty.c kernel/string.c)
pomelo_user(unet servers/net/net.c servers/net/nic.c servers/net/arp.c
    servers/net/ip.c servers/net/sock.c servers/net/sock_reply.c
    kernel/string.c)
pomelo_user(unetsh apps/netsh/netsh.c kernel/string.c)

# ---------------------------------------------------------------------------
# kernel: i386 freestanding ELF, loaded by `qemu-system-i386 -kernel`.
# No libc, no host runtime: the whole point is "compile what you wrote".
# ---------------------------------------------------------------------------
set(KSRC
    boot/boot.S boot/gdt.c boot/main.c
    kernel/char.c kernel/kprintf.c kernel/mouse.c kernel/string.c kernel/vga.c
    kernel/intr/idt.c kernel/intr/intr.c kernel/intr/irq.c
    kernel/intr/stubs.S
    kernel/mm/frame.c kernel/mm/paging.c
    kernel/sched/sched.c kernel/sched/switch.S kernel/sched/prog.c
    kernel/ipc/ipc.c
    kernel/syscall/syscall.c
    ${CMAKE_BINARY_DIR}/ufs_blob.o
    ${CMAKE_BINARY_DIR}/utty_blob.o
    ${CMAKE_BINARY_DIR}/ushell_blob.o
    ${CMAKE_BINARY_DIR}/uprobe_blob.o
    ${CMAKE_BINARY_DIR}/unet_blob.o
    ${CMAKE_BINARY_DIR}/unetsh_blob.o)

set_source_files_properties(${CMAKE_BINARY_DIR}/ufs_blob.o
                            ${CMAKE_BINARY_DIR}/utty_blob.o
                            ${CMAKE_BINARY_DIR}/ushell_blob.o
                            ${CMAKE_BINARY_DIR}/uprobe_blob.o
                            ${CMAKE_BINARY_DIR}/unet_blob.o
                            ${CMAKE_BINARY_DIR}/unetsh_blob.o
                            PROPERTIES GENERATED TRUE)

add_executable(kernel ${KSRC})
add_dependencies(kernel ufs_blob utty_blob ushell_blob uprobe_blob unet_blob
                 unetsh_blob)
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
        -netdev user,id=n0,hostfwd=tcp::2323-:2323 -device e1000,netdev=n0
        -object filter-dump,id=f0,netdev=n0,file=${CMAKE_BINARY_DIR}/net.pcap
    DEPENDS kernel image)

enable_testing()
set(KERNEL_ELF ${CMAKE_BINARY_DIR}/kernel)

# The QEMU tests share one disk image, and the walkthrough writes into
# it. A fixture rebuilds the image from the seed directory at the start
# of each ctest run, so one round's walk.txt never bleeds into the next
# (and `ls` keeps listing the seed's two files instead of a growing
# tail of test leftovers -- see todo.md, image-pollution finding).
add_test(NAME fresh-image
    COMMAND $<TARGET_FILE:mkfs> ${IMG} ${CMAKE_SOURCE_DIR}/tools/mkfs/seed)
set_tests_properties(fresh-image PROPERTIES FIXTURES_SETUP pomelo-image)

add_test(NAME smoke
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke.log)
add_test(NAME smoke-ps2
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke-ps2.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke-ps2.log)
add_test(NAME smoke-mouse
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke-mouse.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke-mouse.log)
add_test(NAME smoke-net
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke-net.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke-net.log)
add_test(NAME smoke-clear
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke-clear.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke-clear)
add_test(NAME smoke-reboot
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke-reboot.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} smoke-reboot.log)
add_test(NAME walkthrough
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/walkthrough.sh
            ${QEMU} ${KERNEL_ELF} ${IMG} walkthrough.log)
set_tests_properties(smoke smoke-ps2 smoke-mouse smoke-net smoke-clear
    smoke-reboot walkthrough PROPERTIES TIMEOUT 120
    FIXTURES_REQUIRED pomelo-image)

# Host-side unit tests for the IPC and frame hardening added in the
# November audit. They compile the production translation units (fsrw.c /
# the five net modules) plus kernel/string.c and link against the host
# 32-bit libc, so the guards under test are the same ones the services
# run -- no source inclusion, no test-only branches. -m32 needs
# gcc-multilib. Probe for it at configure time so a host without the
# 32-bit runtime degrades gracefully (skips these unit tests) instead of
# failing the whole build.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-m32")
check_c_source_compiles("int main(void){return 0;}" POMELO_CAN_LINK_M32)
unset(CMAKE_REQUIRED_FLAGS)
if(POMELO_CAN_LINK_M32)
    add_executable(unit-fs tests/unit/test_fs.c
        servers/fs/fsrw.c kernel/string.c)
    target_compile_options(unit-fs PRIVATE -m32 -O0 -g -Wall -Wextra)
    target_link_options(unit-fs PRIVATE -m32)
    target_include_directories(unit-fs PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME unit-fs COMMAND unit-fs)

    add_executable(unit-net tests/unit/test_net.c
        servers/net/net.c servers/net/nic.c servers/net/arp.c
        servers/net/ip.c servers/net/sock.c kernel/string.c)
    target_compile_options(unit-net PRIVATE -m32 -O0 -g -Wall -Wextra)
    target_link_options(unit-net PRIVATE -m32)
    target_include_directories(unit-net PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME unit-net COMMAND unit-net)

    # unit-irq-guard: the syscall-boundary gates are header inlines, so
    # each test needs no production TU -- it checks the predicate the
    # kernel runs, exactly as written.
    add_executable(unit-irq-guard tests/unit/test_irq_guard.c)
    target_compile_options(unit-irq-guard PRIVATE -m32 -O0 -g -Wall -Wextra)
    target_link_options(unit-irq-guard PRIVATE -m32)
    target_include_directories(unit-irq-guard PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME unit-irq-guard COMMAND unit-irq-guard)

    add_executable(unit-mmio-guard tests/unit/test_mmio_guard.c)
    target_compile_options(unit-mmio-guard PRIVATE -m32 -O0 -g -Wall -Wextra)
    target_link_options(unit-mmio-guard PRIVATE -m32)
    target_include_directories(unit-mmio-guard PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME unit-mmio-guard COMMAND unit-mmio-guard)

    # unit-tty-puts: the MSG_TTY_PUTS length clamp is a header inline,
    # so no production TU -- it checks the rule the service runs.
    add_executable(unit-tty-puts tests/unit/test_tty_puts.c)
    target_compile_options(unit-tty-puts PRIVATE -m32 -O0 -g -Wall -Wextra)
    target_link_options(unit-tty-puts PRIVATE -m32)
    target_include_directories(unit-tty-puts PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME unit-tty-puts COMMAND unit-tty-puts)
else()
    message(STATUS "32-bit host libc (gcc-multilib) not found; "
        "skipping unit-fs/unit-net")
endif()

add_test(NAME unit-mkfs
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/unit/test_mkfs.sh $<TARGET_FILE:mkfs>)
