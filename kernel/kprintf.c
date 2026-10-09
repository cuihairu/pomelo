#include "types.h"
#include "kprintf.h"
#include "vga.h"

/* Kernel-side console: mirrors to both the VGA screen and COM1, so
 * boot and panic messages are visible from `qemu -serial stdio` even
 * before the tty service exists. User tasks print through servers/tty. */


static void ser_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20)) { }   /* wait for THR empty */
    outb(0x3F8, c);
}

void console_putc(char c) {
    vga_putc(c);
    ser_putc(c);
}

static void emit(const char *s, u32 n) {
    for (u32 i = 0; i < n; i++)
        console_putc(s[i]);
}

/* Print a number in the given base, filling backwards then reversing. */
static void emit_num(u32 v, u32 base, int upper) {
    char buf[32];
    static const char *lo = "0123456789abcdef";
    static const char *hi = "0123456789ABCDEF";
    const char *digits = upper ? hi : lo;
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = digits[v % base]; v /= base; }
    while (i--) console_putc(buf[i]);
}

void kprintf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { console_putc(*p); continue; }
        p++;
        switch (*p) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            emit(s, strnlen(s, 512));
            break;
        }
        case 'c': console_putc((char)__builtin_va_arg(ap, int)); break;
        case 'd': {
            i32 v = __builtin_va_arg(ap, i32);
            if (v < 0) { console_putc('-'); v = -v; }
            emit_num((u32)v, 10, 0);
            break;
        }
        case 'u': emit_num(__builtin_va_arg(ap, u32), 10, 0); break;
        case 'x': emit_num(__builtin_va_arg(ap, u32), 16, 0); break;
        case '%': console_putc('%'); break;
        default:  console_putc('%'); console_putc(*p);
        }
    }
    __builtin_va_end(ap);
}
