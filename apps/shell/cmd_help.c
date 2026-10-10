#include "shell.h"

/* Keep the list in the same order as the table in shell.c. */
void cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    tty_puts("commands:\n");
    tty_puts("  help            this text\n");
    tty_puts("  ls              list files on the pomelo disk\n");
    tty_puts("  cat <file>      print a file\n");
    tty_puts("  write <f> <t>   create a file with text\n");
    tty_puts("  echo <words>    print the words back\n");
    tty_puts("  clear           clear the screen\n");
    tty_puts("  ps              list the task table\n");
    tty_puts("  spawn <name>    start a program (fs, tty, shell, probe)\n");
    tty_puts("  uptime          seconds since boot\n");
    tty_puts("  reboot          restart the machine\n");
}
