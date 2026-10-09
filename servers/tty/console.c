#include "../../kernel/types.h"
#include "../../kernel/vga.h"
#include "../../kernel/intr/intr.h"
#include "tty.h"

/* console.c: the serial half of the console and the wiring that makes
 * the tty own both inputs. Output goes to the VGA screen and to COM1
 * (QEMU forwards that to the host terminal); input from either the
 * keyboard or the serial port lands in the character ring in kbd.c. */

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);     /* interrupts off while configuring */
    outb(COM1 + 3, 0x80);     /* DLAB on */
    outb(COM1 + 0, 0x01);     /* divisor 1: 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);     /* 8N1 */
    outb(COM1 + 2, 0xC7);     /* FIFO on, clear */
    outb(COM1 + 4, 0x0B);
    outb(COM1 + 1, 0x01);     /* raise IRQ4 when a byte arrives */
}

static void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) { }
    outb(COM1, c);
}

void tty_putc(char c) {
    serial_putc(c);
    vga_putc(c);
}

/* Serial console: QEMU forwards `-serial stdio` bytes here, so the
 * machine is fully drivable without a GUI keyboard. */
static void serial_isr(void) {
    while (inb(COM1 + 5) & 0x01) {          /* data ready */
        char c = inb(COM1);
        if (c == '\r') c = '\n';            /* terminals send CR */
        kbd_push(c);
    }
}

void console_init(void) {
    serial_init();
    irq_install(IRQ_KBD, kbd_isr);    /* the tty owns both inputs */
    irq_install(IRQ_COM1, serial_isr);
    while (inb(COM1 + 5) & 0x01)      /* drain what the UART buffered */
        serial_isr();
}
