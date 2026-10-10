#include "shell.h"

/* Commands that talk to the machine itself. clear goes through tty
 * (the screen is tty's device); reboot goes through the syscall gate
 * (only the kernel may reset it). */

void cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    tty_clear();
}

void cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    tty_puts("rebooting...\n");
    sys_reboot();
}

void cmd_uptime(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char num[12];
    num_str(sys_uptime(), num);
    tty_puts("up ");
    tty_puts(num);
    tty_puts("s\n");
}

void cmd_mem(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct mem_info m;
    sys_mem(&m);
    char free_n[12], total_n[12];
    num_str(m.free, free_n);
    num_str(m.total, total_n);
    tty_puts("frames: ");
    tty_puts(free_n);
    tty_puts(" of ");
    tty_puts(total_n);
    tty_puts(" free\n");
}
