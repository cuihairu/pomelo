> [Chinese](tty-server.md) | **English**

# 09 · The terminal service

Corresponding code: `servers/tty/`, `kernel/char.c`

## Screen and keyboard, split in two

The hardware of this chapter changes hands. The input side — the keyboard (IRQ1) and the serial port (IRQ4) — moves into the kernel's `kernel/char.c`: the interrupt handler has to run in "the copy of the world that the current cr3 points at," and which task is running at the time is anyone's guess, so code that touches ports and takes over IRQs can only live in the kernel, which all address spaces share (the reason is explained fully in [Chapter 14](/guide/ring3.en)). The output side — VGA and COM1 — is for any task nothing but one syscall: `sys_write`.

So tty becomes a pure ring 3 program that does not touch a single port and only knows messages:

- Receives `TTY_CHAR` from the kernel: one character at a time, from the keyboard or from the serial port;
- Says only three things to the outside: print (`TTY_PUTS`), clear the screen (`TTY_CLEAR`), ask for a line (`TTY_GETLINE`);
- Output goes through `sys_write`; it does not know how the screen lights up.

## Input: an interrupt becomes a message

The interrupt handler in the kernel cannot get any shorter — look up the table, turn it into a character, deliver a message:

```c
static void tty_char(char c) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_CHAR;
    m.data[0] = c;
    ipc_send_kernel(TID_TTY, &m);  /* sent by the kernel, src=0 */
}
```

The serial port goes down the same path: QEMU hands the bytes that `-serial stdio` receives to the same delivery function — so this machine can be driven completely even without a GUI keyboard. **The interrupt handler does only three lines** (read the port, decode, deliver), and the real logic stays entirely in a task that can sleep and wake; it is just that "waiting for input" no longer means tty sleeping on the IRQ, but the kernel delivering the character right to its door.

Before delivery there is one more gate, the most easily overlooked line of this chapter:

```c
static void uart_isr(void) {
    while ((inb(CON_COM1 + 5) & 0x01) && ipc_can_send(TID_TTY)) {
        ...
    }
}
```

tty's inbox is finite, and tty only drains it when the scheduler comes around to it — pasting a whole line into the terminal (QEMU's UART FIFO is also only 16 bytes) overloads it in an instant. The rule is therefore nailed down: **a byte that the queue cannot hold is not allowed to be read out of the FIFO at all**. Bytes left in the FIFO are not lost; reading them out and then throwing them away is what loses them; the clock interrupt retries the backed-up input every time (`char_retry`), and the keyboard has its own 1-slot holding cell. This is not a newly invented mechanism, just the promise at the start of the tty chapter carried all the way through: however slow the reader is, the typist must not lose a keystroke.

## The line protocol: shell wants lines, not characters

shell does not want to handle backspace one character at a time. tty takes this dirty work on itself:

```c
static void handle_char(char c) {
    if (c == '\b') {
        if (len) {
            len--;
            put('\b'); put(' '); put('\b');
        }
    } else if (c == '\n') {
        put('\n');
        if (done_cnt < DONE_CAP) {      /* claim it or drop the newline */
            int tail = (done_head + done_cnt) % DONE_CAP;
            done.len[tail] = len;
            memcpy(done.buf[tail], line, len);
            done_cnt++;
        }
        len = 0;
        deliver();
    } else if ((u8)c >= 32 && len < (int)sizeof(line) - 1) {
        line[len++] = c;
        put(c);                         /* echo: the screen shows what we choose */
    }
}
```

`deliver` checks: is a shell blocking on `TTY_GETLINE` right now? If yes, the moment a line is finished the message goes back immediately; if no, the whole line is stashed in the buffer and waits for shell to come and ask for it. **Producer and speed are decoupled** — you type as fast as you like, shell is as slow as it likes, the line buffer catches all of it.

## The main loop: sleep until the kernel sends a letter

```c
void tty_main(void) {
    struct msg m;
    for (;;) {
        while (sys_try_recv(&m) == 0)   /* drain first: chars typed while busy */
            handle(&m);
        deliver();
        sys_recv(&m);                   /* nothing left: sleep for the kernel */
        handle(&m);
    }
}
```

Drain the inbox first, then block and sleep — and the first thing when you wake up is to handle the one that just arrived. This "drain then sleep" ordering comes from the same root as the lesson of [Chapter 3](/guide/interrupts.en): **never go to sleep holding an unread letter**.

## Echo

The characters you type show up on the screen because tty proactively `put(c)`s them back when it receives them — not because the hardware does it for it. Backspace can erase a character because tty, on `\b`, outputs **backspace, space, backspace again** — three characters (`put('\b'); put(' '); put('\b')`) — step back one cell, cover the original glyph with a space, and step back again. Every single detail of the terminal experience is one line of code inside this loop.

## Files of this chapter

```
kernel/char.c/.h     console hardware: what comes in from COM1+PS/2 becomes a message, what goes out becomes a syscall
servers/tty/tty.h    message types (TTY_PUTS / TTY_GETLINE / TTY_CLEAR / TTY_CHAR)
servers/tty/tty.c    the service loop, the line buffer, answering shell
```

The next chapter: [shell](/guide/shell.en).
