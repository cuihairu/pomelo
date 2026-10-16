> [Chinese](mouse.md) | **English**

# 12 · The PS/2 mouse

Corresponding code: `kernel/mouse.c`, `kernel/vga.c`, `servers/tty/tty.c`

## The second 8042 device

[Chapter 9](/guide/tty-server.en) already set the division: keyboard and serial interrupts land in the kernel, and services only know messages. The mouse is the second device behind the same 8042 controller and follows this division exactly (`kernel/mouse.c`). The one extra step it adds is **initialization**: this is the first device in this kernel that has to be "talked into" sending interrupts before it does — it has no driver, only a protocol.

## The controller: one command byte, two channels

The 8042 has only two ports: write `0x64` to send a command, read `0x64` to see status; all data goes through `0x60`. Keyboard and mouse share this one data port, and bit 5 of the status word says which device is talking. The controller's own command byte governs three things, and the mouse needs two of them:

| bit | What it does |
| --- | --- |
| 0 | the keyboard's IRQ line |
| 1 | the mouse's IRQ line (IRQ12) |
| 5 | set to 1 it stops the mouse clock; it must be cleared or the mouse will not work |

The command byte has to be read and then modified:

```c
outb(PS2_CMD, CMD_GET_CB);            /* 0x20: read the command byte */
wait_read(&cb);
cb |= 0x01 | 0x02;                    /* let both irq1 and irq12 through */
cb &= (u8)~0x20;                      /* the mouse clock runs again */
outb(PS2_CMD, CMD_SET_CB);            /* 0x60: write it back */
outb(PS2_DATA, cb);
```

(The excerpt omits the wait_write before each outb — the real code carries it on every step; if the controller does not come, it reports the error and leaves.)

While modifying, both devices are paused first (`0xAD`/`0xA7`), otherwise half-byte input gets mistaken for keystrokes. Finally `0xAE` and `0xA8` are dialed back once each — besides releasing the mouse clock again, `0xA8` is also the switch that "opens the aux port". Note that the 8042 layer only handles letting traffic through; the real interrupt outlet is the 8259, and that half is finished in `pic_remap`: on the slave PIC only the IRQ12 line is opened, all the rest are off. The reason is the same as the one given for the keyboard in [Chapter 3](/guide/interrupts.en) — a line left open with no one managing it: the moment a signal arrives it walks straight through an empty gate into an exception.

## The handshake: three commands, three replies

The mouse's own protocol goes through the aux channel: write `0xD4` to `0x64`, and only the **next** write to `0x60` counts as addressed to the mouse; reading a reply has to wait until `0xFA`:

```c
static int mouse_cmd(u8 cmd) {
    if (!wait_write()) return 0;
    outb(PS2_CMD, AUX_SELECT);        /* 0xD4: the next 0x60 write goes to the mouse */
    if (!wait_write()) return 0;
    outb(PS2_DATA, cmd);
    for (int i = 0; i < POLL_SPINS; i++) {
        u8 reply;
        if (!wait_read(&reply)) return 0;
        if (reply == MOUSE_ACK) return 1;   /* 0xFA */
    }
    return 0;
}
```

The order is fixed: first `0xF6` to return to the default parameters, then `0xF4` to turn on data reporting, and finally `0xF2` to ask "who are you" and read back the device ID. Each of the first two has one reply — no reply, print one line and give up; the third is the exception — if the identity cannot be got out of it, record `0xFF` and report it anyway, initialization proceeds as normal:

```c
if (!mouse_cmd(MOUSE_ENABLE_DATA)) {
    kprintf("mouse: no ack for enable, giving up\n");
    return;
}
```

This step deserves a word of its own: the silent behavior when mouse initialization fails is exactly the same as "there is a bug somewhere else in the kernel". So every wait has an upper bound (100,000 poll spins); on timeout it reports the truth and the machine carries on as before — no mouse, everything else as normal. A device ID other than 0 is also printed but does not block: if the host never runs the scroll-wheel enable sequence, even a scroll-wheel mouse reports `0x00`; reporting a different value means it has already been switched into extended mode, and while the extended protocol is not enabled it still sends three-byte packets.

## Data packets: three bytes, one sample

What the interrupt gets is a byte stream, not ready-made events one by one. Three bytes make one packet:

| Byte | bit | Meaning |
| --- | --- | --- |
| 0 | 0,1,2 | left / right / middle button |
| 0 | 3 | always 1: the sync flag |
| 0 | 6,7 | x / y displacement overflow |
| 1 | whole byte | dx, two's complement |
| 2 | whole byte | dy, two's complement; positive means up |

The sync flag is this byte stream's only chance to re-align: lose one byte in the middle of a packet and the following two no longer line up at all. So in the ISR bit 3 is checked on the first byte only; if it does not match, drop it and count again:

```c
if (pkt_at == 0 && !(b & 0x08)) return;      /* sync bit lost, count again */
pkt[pkt_at++] = b;
if (pkt_at < 3) return;
pkt_at = 0;

int dx = (int)(pkt[1] ^ 0x80) - 0x80;         /* sign-extend to a full int */
int dy = (int)(pkt[2] ^ 0x80) - 0x80;
```

`^0x80 - 0x80` is one way to sign-extend a two's complement low byte: flip the sign bit and then subtract it, and both positive and negative values come out right — one branch fewer than "look at bit 7 and then subtract 256".

The overflow bits are the hardware saying "the counter ran past ±255, this number is not trustworthy". This kernel does not pretend to know the answer: it zeroes the displacement outright and still reports the button bits as usual — reporting a fake displacement is worse than reporting nothing, because the caller has no way to tell the difference. Also, in the protocol y is positive upward while the screen's row numbers grow downward, so it is inverted once before going into the cursor.

## The cursor: one move touches exactly two cells

The block cursor on the text buffer borrows the cell itself: the character stays exactly as it is and only the attribute byte is flipped to the highlight. That gives a strict "borrow-return" transaction — on leaving, the saved original is put back; on the landing cell, the new cell is borrowed in turn. One move touches exactly two cells, and not one other byte of the screen changes. Out of bounds is clamped, not wrapped to the next line — and that is why the cell count is always exactly two: once it wrapped, the borrow would jump a whole row.

```c
#define CURSOR_ATTR 0x70                /* black-on-white: the highlight */

void vga_mouse_move(int dx, int dy) {
    vga[cur_y * COLS + cur_x] = cur_saved;      /* give the borrowed one back */
    cur_x += dx;
    cur_y += dy;
    if (cur_x < 0) cur_x = 0;
    if (cur_x >= COLS) cur_x = COLS - 1;
    if (cur_y < 0) cur_y = 0;
    if (cur_y >= ROWS) cur_y = ROWS - 1;
    cursor_borrow();
}
```

One known simplification, written down here so the next person does not run into it again: when text is written into the cell the cursor is currently occupying, `cur_saved` already holds the old contents, and the next move writes the old character back. On real hardware either refresh the cursor in passing inside `vga_putc`, or let the cursor follow the end of the line — this kernel does neither, and on screen you will see the cursor occasionally "eat" a frame of the old ghost.

## The message: the fifth thing said to tty

The cursor is drawn by the kernel, but the sample itself also has to be delivered, through the same channel as the keyboard and to the same recipient:

```c
struct msg m;
memset(&m, 0, sizeof m);
m.type = MSG_TTY_MOUSE;
m.arg0 = dx;
m.arg1 = dy;
m.arg2 = pkt[0] & 0x07;               /* buttons, one bit each */
ipc_send_kernel(TID_TTY, &m);
```

tty's current decision is to drop it: `MSG_TTY_GETLINE` speaks in lines, shell speaks in lines, and there is no party that takes coordinates. The decision belongs to tty rather than the kernel, so the handling is written as an explicit branch instead of quietly disappearing into the default branch — when forwarding is wanted one day, this is the one place to change. shell knows nothing about it; its behavior is not changed by a single character.

One more gate in the ISR while we are at it: the keyboard can hold its cell and retry on the clock (that move in `char.c`); the mouse cannot — when the queue is full, the current sample is dropped. The reason differs — keystrokes are discrete, so losing one is a real loss; motion is a continuous stream, and the next frame overwrites this frame's conclusion. Whether dropping is right depends on whether the event itself will be superseded by the next one.

## Verified: smoke-mouse

The automation procedure is exactly the same as `smoke-ps2.sh`; only the input changes device and the evidence changes byte: the monitor sends `mouse_move 15 6`, and the cursor cell on screen must land on the predicted (55,18) — that is, exactly one cell whose attribute byte is `0x70`; then send `mouse_move -5 -2`, the new landing cell is (50,16) showing `0x70`, while (55,18) must have gone back to the default `0x07`. **The second assertion is the key one**: the first only proves something was drawn, the second proves "only two cells move". In between sits a `mouse_button 1/0` pair, and one more move must still line up the same way — button events travel the same ISR and the system does not get sidetracked by them.

The cursor position is computed, not measured: initialization stops at the center (40,12), displacement accumulates frame by frame and is clamped within 80×25, so the answers for all three moves are hardcoded in the script. Running QEMU's mouse on real hardware, what you see is this block.

## Files of this chapter

```
kernel/mouse.c         the 8042 handshake and command byte, the IRQ12 ISR, three-byte decoding, message delivery
kernel/mouse.h         declarations
kernel/vga.c/.h        the block cursor's borrow-return transaction and in-screen clamping
kernel/intr/intr.c     opening only the IRQ12 line on the slave PIC
servers/tty/tty.h      MSG_TTY_MOUSE
servers/tty/tty.c      the explicit drop: whether to forward is tty's decision
tools/smoke-mouse.sh   smoke test: the monitor sends events, VGA attribute-byte forensics
```

The next chapter: [Paging comes online](/guide/paging.en).
