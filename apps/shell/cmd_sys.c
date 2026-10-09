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
