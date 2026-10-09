#include "../../kernel/string.h"
#include "../../servers/fs/format.h"
#include "shell.h"

/* The file commands. Every one of them is just message round trips:
 * ls streams entries, cat reads chunks, write creates then commits. */

void cmd_ls(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char name[NAMELEN + 1], num[12];
    int size;
    fs_ls_begin();
    while (!fs_ls_next(name, &size)) {
        tty_puts(" ");
        tty_puts(name);
        tty_puts(" (");
        num_str(size, num);
        tty_puts(num);
        tty_puts(")\n");
    }
}

void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        tty_puts("usage: cat <file>\n");
        return;
    }
    int size;
    int ino = fs_open(argv[1], &size);
    if (ino < 0) {
        tty_puts("no such file\n");
        return;
    }
    char buf[MSG_DATA];
    for (int off = 0; off < size; ) {
        int n = fs_read(ino, off, buf);
        if (n <= 0) break;
        tty_write(buf, n);
        off += n;
    }
}

void cmd_write(int argc, char **argv) {
    if (argc < 3) {
        tty_puts("usage: write <file> <text...>\n");
        return;
    }
    static char content[MSG_DATA * 8];
    int total = 0;
    for (int i = 2; i < argc; i++) {          /* join args with spaces */
        if (i > 2) content[total++] = ' ';
        int n = strnlen(argv[i], sizeof(content) - total - 2);
        memcpy(content + total, argv[i], n);
        total += n;
    }
    content[total++] = '\n';

    int ino = fs_create(argv[1]);
    if (ino < 0) {
        tty_puts("no space left\n");
        return;
    }
    for (int off = 0; off < total; ) {
        int n = total - off > MSG_DATA ? MSG_DATA : total - off;
        off += fs_write(ino, off, content + off, n);
    }
    fs_commit(ino, total);

    char num[12];
    tty_puts("wrote ");
    num_str(total, num);
    tty_puts(num);
    tty_puts(" bytes to ");
    tty_puts(argv[1]);
    tty_puts("\n");
}
