#include "shell.h"

/* ps and spawn: the shell as the machine's front desk. ps reads a
 * snapshot of the task table through the syscall gate; spawn asks the
 * kernel to start a registered program by name -- this is how a dead
 * service comes back, and how the probe gets a second life. */

static const char *state_name(int s) {
    switch (s) {
    case ST_FREE:    return "free";
    case ST_READY:   return "ready";
    case ST_BLOCKED: return "blocked";
    }
    return "?";
}

void cmd_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct ps_entry e[NTASK];
    int n = sys_ps(e, NTASK);
    tty_puts("tid program  state\n");
    for (int i = 0; i < n; i++) {
        char line[40];
        int p = 0;
        line[p++] = ' ';
        line[p++] = '0' + e[i].tid;
        line[p++] = ' ';
        if (e[i].name[0]) {
            for (int j = 0; e[i].name[j]; j++) line[p++] = e[i].name[j];
        } else {
            line[p++] = '-';
        }
        while (p < 12) line[p++] = ' ';
        const char *st = state_name(e[i].state);
        for (int j = 0; st[j]; j++) line[p++] = st[j];
        line[p++] = '\n';
        line[p] = 0;
        tty_write(line, p);
    }
}

void cmd_spawn(int argc, char **argv) {
    if (argc < 2) {
        tty_puts("usage: spawn <fs|tty|shell|probe>\n");
        return;
    }
    int tid = sys_spawn(argv[1]);
    if (tid < 0) {
        tty_puts("spawn failed (unknown name or no seat)\n");
        return;
    }
    char line[48];
    int p = 0;
    for (int i = 0; argv[1][i]; i++) line[p++] = argv[1][i];
    for (const char *s = " is task "; *s; s++) line[p++] = *s;
    line[p++] = '0' + tid;
    for (const char *s = " now\n"; *s; s++) line[p++] = *s;
    line[p] = 0;
    tty_write(line, p);
}
