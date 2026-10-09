#include "../../kernel/types.h"
#include "tty.h"

/* kbd.c: every input byte, whatever its source, lands in one character
 * ring; the tty service drains it with kbd_getc. Keeping the ring here
 * lets the keyboard and the serial port share it without knowing about
 * each other. */

static char kbuf[64];
static int kh, kt, kcnt;
static int shift, skip_ext;

static char map_scan(u8 s) {
    if (s == 0x1C) return '\n';
    if (s == 0x0E) return '\b';
    if (s == 0x39) return ' ';
    if (s >= 0x02 && s <= 0x0B)
        return shift ? ")!@#$%^&*("[s - 0x02] : "1234567890"[s - 0x02];
    if (s >= 0x10 && s <= 0x19) {
        char c = "qwertyuiop"[s - 0x10];
        return shift ? c - 32 : c;
    }
    if (s >= 0x1E && s <= 0x26) {
        char c = "asdfghjkl"[s - 0x1E];
        return shift ? c - 32 : c;
    }
    if (s >= 0x2C && s <= 0x32) {
        char c = "zxcvbnm"[s - 0x2C];
        return shift ? c - 32 : c;
    }
    switch (s) {
    case 0x0C: return shift ? '_' : '-';
    case 0x0D: return shift ? '+' : '=';
    case 0x33: return shift ? '<' : ',';
    case 0x34: return shift ? '>' : '.';
    case 0x35: return shift ? '?' : '/';
    }
    return 0;
}

void kbd_push(char c) {
    if (kcnt >= (int)sizeof(kbuf)) return;
    kbuf[kt] = c;
    kt = (kt + 1) % (int)sizeof(kbuf);
    kcnt++;
}

/* Interrupt context: three lines of work, then out. */
void kbd_isr(void) {
    u8 s = inb(0x60);
    if (s == 0xE0) { skip_ext = 1; return; }   /* extended prefix */
    if (skip_ext)       { skip_ext = 0; return; }
    if (s == 0x2A || s == 0x36) { shift = 1; return; }
    if (s == 0xAA || s == 0xB6) { shift = 0; return; }
    if (s & 0x80) return;                      /* key release */
    char c = map_scan(s);
    if (c) kbd_push(c);
}

int kbd_getc(void) {
    if (!kcnt) return -1;
    char c = kbuf[kh];
    kh = (kh + 1) % (int)sizeof(kbuf);
    kcnt--;
    return (u8)c;
}
