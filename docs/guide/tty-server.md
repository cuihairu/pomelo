# 09 · 终端服务

对应代码:`servers/tty/`

## 终端也是一个服务

屏幕和键盘没有任何特殊之处:它们是硬件,硬件该归服务管。tty 服务拥有
`console.c`(字符设备)和 `tty.c`(行协议),职责分明:

- **console**:扫描码 → 字符,字符 → 屏幕;
- **tty**:把字符攒成行,按需交给 shell。

## 输出:往两个地方写字节

```c
void console_putc(char c) {
    serial_putc(c);        /* 串口 0x3F8:QEMU 会把它转发到宿主终端 */
    vga_putc(c);           /* VGA 文本缓冲 0xB8000:QEMU 窗口里可见 */
}
```

VGA 文本模式把每个字符写成 2 字节(字符 + 颜色)放进固定地址 `0xB8000`,
80×25 共 4000 字节。这是 x86 上最古老的“显示协议”,写起来意外地简单:
算偏移,写字节,滚动时 `memmove`。

## 键盘:中断 → 扫描码 → 字符

键盘中断是 IRQ1(36 号向量)。tty 注册自己的处理函数,把它变成可等待的事件:

```c
static void kbd_irq(void) {
    u8 scan = inb(0x60);           /* 0x60 号端口 = 键盘数据 */
    if (scan & 0x80) return;       /* 高位=松开,忽略 */
    ring_put(scan_to_char(scan));  /* 查表:扫描码 → ASCII */
}

void tty_main(void) {
    irq_install(1, kbd_irq);
    for (;;) {
        irq_wait(1);               /* 睡到下一个按键 */
        /* ...从 ring 取字符,处理行逻辑... */
    }
}
```

注意分工:**中断函数只做三行**(读端口、丢松键、入队),真正的逻辑在任务循环里。
这就是第 3 章 `irq_wait` 原语的用途——让中断保持短小,让逻辑留在能睡能醒的任务里。

## 行协议:shell 要的是行,不是字符

shell 不想一个字符一个字符地处理退格。tty 把这些脏活揽下来:

```c
for (;;) {
    irq_wait(1);
    while ((c = ring_get()) >= 0) {
        if (c == '\b')  line_edit_backspace();
        else if (c == '\n')  { line_complete(); serve_pending(); }
        else                 line_edit_append(c);
    }
}
```

`serve_pending` 检查:有没有 shell 正阻塞在 `TTY_GETLINE` 上?有,就立刻回消息;
没有,把整行存进缓冲,等 shell 来要。**生产者和速度解耦**——你打字再快,
shell 再慢,行缓冲都兜着。

## 回显

你敲的字符会出现在屏幕上,是因为 tty 收到后主动 `console_putc(c)` 回显——
不是硬件替它做的。退格能抹掉字符,也是 tty 在 `\b` 时输出 `" \b"` 三个字符
把光标推回去再抹。终端体验的每一处细节,都是这个循环里的一行代码。

## 本章文件

```
servers/tty/tty.h       消息类型(TTY_PUTS / TTY_GETLINE / TTY_CLEAR)
servers/tty/console.c   VGA + 串口输出、键盘扫描码处理
servers/tty/tty.c       服务循环、行缓冲、应答 shell
```

下一章:[shell](/guide/shell)。
