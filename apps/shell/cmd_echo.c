#include "shell.h"

/* echo: the smallest command in the table, and the template for the
 * next one you write. It touches no service but tty -- argv in, one
 * message out. */

void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) tty_puts(" ");
        tty_puts(argv[i]);
    }
    tty_puts("\n");
}
