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

/* --- the mouse cursor: one block, two cells of footprint --------------- */
/* A block cursor in a text buffer is a borrowed cell: the character
 * underneath stays readable, only the attribute is inverted. That makes
 * the move a strict two-cell transaction -- put the saved cell back
 * where we were, borrow the cell where we land -- and nothing else on
 * screen is ever touched, no matter how far one sample jumps. Clamping
 * (not wrapping) at the edges is what keeps the count at two: a wrap
 * would smear the borrow across a row boundary. */

#define CURSOR_ATTR 0x70                /* black on white: the highlight */

static int cur_x, cur_y;
static u16 cur_saved;                   /* the borrowed cell, as found */

static void cursor_borrow(void) {
    volatile u16 *cell = &vga[cur_y * COLS + cur_x];
    cur_saved = *cell;
    *cell = (cur_saved & 0xFF) | ((u16)CURSOR_ATTR << 8);
}

void vga_mouse_home(void) {
    cur_x = COLS / 2;
    cur_y = ROWS / 2;
    cursor_borrow();
}

void vga_mouse_move(int dx, int dy) {
    vga[cur_y * COLS + cur_x] = cur_saved;      /* put back what we borrowed */
    cur_x += dx;
    cur_y += dy;
    if (cur_x < 0) cur_x = 0;
    if (cur_x >= COLS) cur_x = COLS - 1;
    if (cur_y < 0) cur_y = 0;
    if (cur_y >= ROWS) cur_y = ROWS - 1;
    cursor_borrow();
}
