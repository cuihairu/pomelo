#include "char.h"
#include "vga.h"
#include "string.h"
#include "intr/intr.h"
#include "ipc/ipc.h"
#include "syscall/syscall.h"
#include "../servers/tty/tty.h"

/* char.c: the console driver, kernel-side. Moving it here is what lets
 * fs and tty live in their own address spaces: interrupt handlers have
 * to run in whatever address space the CPU happens to be in, so the
 * code touching ports and IRQ lines belongs in the one space every cr3
 * maps. Input becomes a message to the tty service; output is one
 * syscall away for any task. */

/* --- output: VGA text plus COM1 (QEMU forwards it to the host) -------- */

static void serial_tx(char c) {
    while (!(inb(CON_COM1 + 5) & 0x20)) { }
    outb(CON_COM1, c);
}

void con_putc(char c) {
    serial_tx(c);
    vga_putc(c);
}

void con_write(const char *s, int n) {
    for (int i = 0; i < n; i++) con_putc(s[i]);
}

void con_clear(void) {
    vga_clear();
}

/* --- input: whatever produces a character sends it to the tty --------- */

static void tty_char(char c) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_CHAR;
    m.data[0] = c;
    ipc_send_kernel(TID_TTY, &m);
}

/* Serial console: QEMU forwards `-serial stdio` bytes here, so the
 * machine is fully drivable without a GUI keyboard. */
static void uart_isr(void) {
    while (inb(CON_COM1 + 5) & 0x01) {      /* data ready */
        char c = inb(CON_COM1);
        if (c == '\r') c = '\n';            /* terminals send CR */
        tty_char(c);
    }
}

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

/* Interrupt context: decode, deliver, out. */
static void kbd_isr(void) {
    u8 s = inb(0x60);
    if (s == 0xE0) { skip_ext = 1; return; }   /* extended prefix */
    if (skip_ext)       { skip_ext = 0; return; }
    if (s == 0x2A || s == 0x36) { shift = 1; return; }
    if (s == 0xAA || s == 0xB6) { shift = 0; return; }
    if (s & 0x80) return;                      /* key release */
    char c = map_scan(s);
    if (c) tty_char(c);
}

void char_init(void) {
    outb(CON_COM1 + 1, 0x00);     /* interrupts off while configuring */
    outb(CON_COM1 + 3, 0x80);     /* DLAB on */
    outb(CON_COM1 + 0, 0x01);     /* divisor 1: 115200 baud */
    outb(CON_COM1 + 1, 0x00);
    outb(CON_COM1 + 3, 0x03);     /* 8N1 */
    outb(CON_COM1 + 2, 0xC7);     /* FIFO on, clear */
    outb(CON_COM1 + 4, 0x0B);
    outb(CON_COM1 + 1, 0x01);     /* raise IRQ4 when a byte arrives */

    irq_install(IRQ_KBD, kbd_isr);
    irq_install(IRQ_COM1, uart_isr);
    while (inb(CON_COM1 + 5) & 0x01)      /* drain what the UART buffered */
        uart_isr();
}
