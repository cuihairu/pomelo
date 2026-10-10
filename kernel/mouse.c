#include "types.h"
#include "kprintf.h"
#include "mouse.h"
#include "vga.h"
#include "string.h"
#include "intr/intr.h"
#include "ipc/ipc.h"
#include "syscall/syscall.h"
#include "../servers/tty/tty.h"

/* mouse.c: the PS/2 mouse, the second device behind the 8042 controller.
 * The keyboard already set the pattern -- hardware the interrupt handler
 * must reach lives in the one address space every cr3 maps -- so the
 * mouse rides it: a polled probe, one small isr, one message to tty.
 * What is new here is the probe itself: this is the first device in the
 * kernel that has to be convinced before it will send interrupts. */

/* --- the 8042: two ports, one command byte, two devices ---------------- */
#define PS2_DATA  0x60            /* data to/from whichever device speaks */
#define PS2_CMD   0x64            /* read: status, write: controller command */
#define STAT_OBF  0x01            /* a byte waits in 0x60 */
#define STAT_IBF  0x02            /* the controller is busy, hold your write */
#define STAT_AUX  0x20            /* the byte in 0x60 came from the aux side */

#define CMD_KBD_DISABLE  0xAD
#define CMD_AUX_DISABLE  0xA7
#define CMD_KBD_ENABLE   0xAE
#define CMD_AUX_ENABLE   0xA8
#define CMD_GET_CB       0x20     /* command byte out through 0x60 */
#define CMD_SET_CB       0x60     /* command byte in through 0x60 */
#define AUX_SELECT       0xD4     /* the next 0x60 write is addressed to the mouse */

/* the mouse's own protocol, spoken over that aux channel */
#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ENABLE_DATA  0xF4
#define MOUSE_GET_ID       0xF2
#define MOUSE_ACK          0xFA

/* Every wait is bounded: a missing device must not hang the boot. The
 * loop gives up, the caller prints why, and the machine goes on without
 * the thing it was waiting for. */
#define POLL_SPINS 100000

static int wait_write(void) {             /* controller ready to take a byte */
    for (int i = 0; i < POLL_SPINS && (inb(PS2_CMD) & STAT_IBF); i++) { }
    return !(inb(PS2_CMD) & STAT_IBF);
}

static int wait_read(u8 *out) {           /* 1: got one, 0: it went quiet */
    for (int i = 0; i < POLL_SPINS && !(inb(PS2_CMD) & STAT_OBF); i++) { }
    if (!(inb(PS2_CMD) & STAT_OBF)) return 0;
    *out = inb(PS2_DATA);
    return 1;
}

/* One command to the mouse and the ack it owes back. Both halves go
 * through 0x60 -- 0xD4 to 0x64 is what steers a write to the aux side.
 * A stray byte on the way back is read and discarded, never mistaken
 * for the ack. */
static int mouse_cmd(u8 cmd) {
    if (!wait_write()) return 0;
    outb(PS2_CMD, AUX_SELECT);
    if (!wait_write()) return 0;
    outb(PS2_DATA, cmd);
    for (int i = 0; i < POLL_SPINS; i++) {
        u8 reply;
        if (!wait_read(&reply)) return 0;
        if (reply == MOUSE_ACK) return 1;
    }
    return 0;
}

/* --- the packet: three bytes, one sample ------------------------------- */

static u8  pkt[3];
static int pkt_at;

/* Interrupt context: one byte out of the aux side, three per sample.
 * Byte 0: bit 3 is always 1 -- the only sync mark a byte stream gets --
 * bits 0/1/2 are the left/right/middle buttons, bits 6/7 say the x/y
 * displacement overflowed. Bytes 1 and 2 are the displacements as
 * two's-complement low bytes. Losing the sync bit is the one error this
 * isr cannot recover from inside a packet, so a byte without it is
 * dropped on the floor and the accumulator restarts. */
static void mouse_isr(void) {
    u8 st = inb(PS2_CMD);
    if (!(st & STAT_OBF)) return;
    if (!(st & STAT_AUX)) return;         /* a keyboard byte: not ours to take */
    u8 b = inb(PS2_DATA);

    if (pkt_at == 0 && !(b & 0x08)) return;
    pkt[pkt_at++] = b;
    if (pkt_at < 3) return;
    pkt_at = 0;

    int dx = (int)(pkt[1] ^ 0x80) - 0x80; /* sign-extend the low byte */
    int dy = (int)(pkt[2] ^ 0x80) - 0x80;
    if (pkt[0] & 0xC0) dx = dy = 0;       /* overflow: the counters lied */
    dy = -dy;                             /* packet y grows up, screen grows down */

    vga_mouse_move(dx, dy);               /* the block on screen is ours */

    /* Motion is a stream, not a message queue: a sample the tty cannot
     * take right now is dropped, because the next one supersedes it. A
     * keystroke deserves a hold slot (char.c); a mouse sample does not. */
    if (!ipc_can_send(TID_TTY)) return;
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_MOUSE;
    m.arg0 = dx;
    m.arg1 = dy;
    m.arg2 = pkt[0] & 0x07;               /* buttons, one bit each */
    ipc_send_kernel(TID_TTY, &m);
}

/* --- the probe --------------------------------------------------------- */

static u8 mouse_id;                       /* what the mouse said it is */

void mouse_init(void) {
    /* Reconfigure the command byte with both devices quiet, so a
     * half-written byte is never mistaken for input. Bit 1 is the aux
     * line's own irq gate (the pic got its half in pic_remap); bit 5
     * must be clear -- on this controller a set bit 5 holds the mouse
     * clock stopped. */
    outb(PS2_CMD, CMD_KBD_DISABLE);
    outb(PS2_CMD, CMD_AUX_DISABLE);
    u8 cb;
    if (!wait_write()) return;
    outb(PS2_CMD, CMD_GET_CB);
    if (!wait_read(&cb)) {
        kprintf("mouse: no command byte, no probe\n");
        outb(PS2_CMD, CMD_KBD_ENABLE);
        return;
    }
    cb |= 0x01 | 0x02;                    /* irq1 and irq12 through the 8042 */
    cb &= (u8)~0x20;                      /* mouse clock runs */
    if (!wait_write()) return;
    outb(PS2_CMD, CMD_SET_CB);
    if (!wait_write()) return;
    outb(PS2_DATA, cb);
    outb(PS2_CMD, CMD_KBD_ENABLE);
    outb(PS2_CMD, CMD_AUX_ENABLE);

    /* Now convince the mouse: defaults, then data reporting, then ask
     * who it is. Each step is acked; a missing ack is printed, not
     * swallowed -- a silent failure here would look exactly like a
     * broken kernel everywhere else. */
    if (!mouse_cmd(MOUSE_SET_DEFAULTS)) {
        kprintf("mouse: no ack for defaults, giving up\n");
        return;
    }
    if (!mouse_cmd(MOUSE_ENABLE_DATA)) {
        kprintf("mouse: no ack for enable, giving up\n");
        return;
    }
    mouse_id = 0xFF;
    if (mouse_cmd(MOUSE_GET_ID)) {
        u8 id;
        if (wait_read(&id)) mouse_id = id;
    }
    if (mouse_id != 0x00)
        kprintf("mouse: id %x, not the standard one\n", mouse_id);

    irq_install(IRQ_MOUSE, mouse_isr);
    vga_mouse_home();
    kprintf("mouse: ps/2 up on irq12\n");
}
