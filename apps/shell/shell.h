#ifndef POMELO_SHELL_H
#define POMELO_SHELL_H

#include "../../kernel/types.h"
#include "../../kernel/syscall/syscall.h"
#include "../../servers/tty/tty.h"
#include "../../servers/fs/fs.h"

/* The shell speaks only two protocols: tty (screen/keyboard) and fs
 * (files). Everything below is either a command or a client wrapper. */

struct cmd {
    const char *name;
    void (*fn)(int argc, char **argv);
};

void cmd_help(int argc, char **argv);
void cmd_ls(int argc, char **argv);
void cmd_cat(int argc, char **argv);
void cmd_write(int argc, char **argv);
void cmd_echo(int argc, char **argv);
void cmd_clear(int argc, char **argv);
void cmd_reboot(int argc, char **argv);
void cmd_ps(int argc, char **argv);
void cmd_spawn(int argc, char **argv);
void cmd_uptime(int argc, char **argv);

/* shell.c: tty client helpers */
void tty_puts(const char *s);
void tty_write(const char *s, int n);
void tty_getline(char *buf, int max);
void tty_clear(void);

/* fsio.c: fs client helpers */
int  fs_open(const char *name, int *size);
int  fs_read(int ino, int off, char *buf);
int  fs_create(const char *name);
int  fs_write(int ino, int off, const char *buf, int n);
int  fs_commit(int ino, int size);
void fs_ls_begin(void);
int  fs_ls_next(char *name, int *size);   /* 0 = entry, 1 = end */
void num_str(int v, char *buf);

#endif
