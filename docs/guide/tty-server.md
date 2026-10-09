# 09 · 终端服务

对应代码:`servers/tty/`

## 终端也是一个服务

屏幕和键盘没有任何特殊之处:它们是硬件,硬件该归服务管。tty 服务拥有三个文件,
职责分明:

- **console.c**:串口配置,输出同时往 VGA 和 COM1 写;
- **kbd.c**:键盘与串口的输入,统统归约成一个字符环;
- **tty.c**:行协议——把字符攒成行,按需交给 shell。

## 输出:往两个地方写字节

```c
void tty_putc(char c) {
    serial_putc(c);        /* 串口 0x3F8:QEMU 会把它转发到宿主终端 */
    vga_putc(c);           /* VGA 文本缓冲 0xB8000:QEMU 窗口里可见 */
}
```

VGA 文本模式把每个字符写成 2 字节(字符 + 颜色)放进固定地址 `0xB8000`,
80×25 共 4000 字节。这是 x86 上最古老的“显示协议”,写起来意外地简单:
算偏移,写字节,滚动时 `memmove`。

## 输入:两条路,一个字符环

键盘中断是 IRQ1。`kbd.c` 里的中断函数把它变成字符,推进一个环:

```c
void kbd_isr(void) {
    u8 s = inb(0x60);              /* 0x60 号端口 = 键盘数据 */
    if (s & 0x80) return;          /* 高位=松开,忽略 */
    char c = map_scan(s);          /* 查表:扫描码 → ASCII */
    if (c) kbd_push(c);            /* 进字符环 */
}
```

串口走的是同一条路:QEMU 把 `-serial stdio` 收到的字节交给 `serial_isr`,
也推进**同一个字符环**——所以这台机器没有 GUI 键盘也能完整驱动。注意分工:
**中断函数只做三行**(读端口、丢松键、入环),真正的逻辑在任务循环里。
这就是第 3 章 `irq_wait` 原语的用途——让中断保持短小,让逻辑留在能睡能醒的
任务里。

## 行协议:shell 要的是行,不是字符

shell 不想一个字符一个字符地处理退格。tty 把这些脏活揽下来:

```c
for (;;) {
    while ((c = kbd_getc()) >= 0)      /* 先清掉已到达的字符 */
        handle_char((char)c);
    pump();                            /* 再处理服务请求(输出、要行) */
    deliver();
    sys_irq_wait(IRQ_KBD | IRQ_COM1);  /* 然后睡到下一个输入 */
}
```

`pump` 检查:有没有 shell 正阻塞在 `TTY_GETLINE` 上?有,行一攒完
(`deliver`)就立刻回消息;没有,把整行存进缓冲,等 shell 来要。
**生产者和速度解耦**——你打字再快,shell 再慢,行缓冲都兜着。

## 回显

你敲的字符会出现在屏幕上,是因为 tty 收到后主动 `tty_putc(c)` 回显——
不是硬件替它做的。退格能抹掉字符,也是 tty 在 `\b` 时输出 `" \b"` 三个字符
把光标推回去再抹。终端体验的每一处细节,都是这个循环里的一行代码。

## 本章文件

```
servers/tty/tty.h       消息类型(TTY_PUTS / TTY_GETLINE / TTY_CLEAR)
servers/tty/console.c   串口配置、双路输出(tty_putc)、输入接线
servers/tty/kbd.c       扫描码表、字符环(kbd_push / kbd_getc)
servers/tty/tty.c       服务循环、行缓冲、应答 shell
```

下一章:[shell](/guide/shell)。
