#include "../../kernel/string.h"
#include "shell.h"

/* REPL: prompt, read one line via tty, dispatch from a table. */

/* The kernel reads the first four bytes of the embedded blob to find
 * this: the entry point of the program (see tools/user.ld). */
void shell_main(void);
__attribute__((section(".hdr")))
const unsigned shell_entry = (unsigned)shell_main;

static const struct cmd cmds[] = {
    { "help",   cmd_help   },
    { "ls",     cmd_ls     },
    { "cat",    cmd_cat    },
    { "write",  cmd_write  },
    { "echo",   cmd_echo   },
    { "clear",  cmd_clear  },
    { "ps",     cmd_ps     },
    { "spawn",  cmd_spawn  },
    { "uptime", cmd_uptime },
    { "mem",    cmd_mem    },
    { "reboot", cmd_reboot },
    { 0, 0 }
};

void tty_write(const char *s, int n) {
    while (n > 0) {                       /* one message per MSG_DATA chunk */
        int c = n > MSG_DATA ? MSG_DATA : n;
        struct msg m;
        memset(&m, 0, sizeof m);
        m.type = MSG_TTY_PUTS;
        m.arg0 = c;
        memcpy(m.data, s, c);
        sys_send(TID_TTY, &m);
        s += c;
        n -= c;
    }
}

void tty_puts(const char *s) {
    tty_write(s, strnlen(s, 512));
}

void tty_getline(char *buf, int max) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_GETLINE;
    sys_send(TID_TTY, &m);                /* order: one line, please */
    sys_recv(&m);                         /* ...and block for the answer */
    int n = m.arg0 < max - 1 ? m.arg0 : max - 1;
    memcpy(buf, m.data, n);
    buf[n] = 0;
}

void tty_clear(void) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_CLEAR;
    sys_send(TID_TTY, &m);
}

/* Split in place on spaces; at most 7 arguments. */
static void run(char *l) {
    char *argv[8];
    int argc = 0;
    for (char *p = l; *p && argc < 7; ) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (!argc) return;
    argv[argc] = 0;
    for (const struct cmd *c = cmds; c->name; c++)
        if (!strncmp(argv[0], c->name, 12)) {
            c->fn(argc, argv);
            return;
        }
    tty_puts("unknown command (try help)\n");
}

void shell_main(void) {
    static char line[64];
    tty_puts("\npomelo shell - type 'help'\n");
    for (;;) {
        tty_puts("pomelo> ");
        tty_getline(line, sizeof line);
        run(line);
    }
}
