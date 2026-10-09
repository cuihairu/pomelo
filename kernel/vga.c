#include "types.h"
#include "vga.h"

#define COLS 80
#define ROWS 25
#define ATTR 0x0700              /* light gray on black */

static volatile u16 *const vga = (u16 *)0xB8000;
static int row, col;

static void sync_cursor(void) {
    u16 pos = row * COLS + col;
    outb(0x3D4, 0x0F); outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E); outb(0x3D5, pos >> 8);
}

static void scroll(void) {
    for (int r = 1; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            vga[(r - 1) * COLS + c] = vga[r * COLS + c];
    for (int c = 0; c < COLS; c++)
        vga[(ROWS - 1) * COLS + c] = ATTR | ' ';
    row = ROWS - 1;
}

void vga_putc(char c) {
    if (c == '\n') {
        col = 0; row++;
    } else if (c == '\b') {
        if (col) col--;
        vga[row * COLS + col] = ATTR | ' ';
    } else {
        vga[row * COLS + col] = ATTR | (u8)c;
        if (++col >= COLS) { col = 0; row++; }
    }
    if (row >= ROWS) scroll();
    sync_cursor();
}

void vga_clear(void) {
    for (int i = 0; i < COLS * ROWS; i++)
        vga[i] = ATTR | ' ';
    row = col = 0;
    sync_cursor();
}
