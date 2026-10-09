# 09 · 终端服务

对应代码:`servers/tty/`、`kernel/char.c`

## 屏幕和键盘,一分为二

这一章的硬件换了主人。输入端——键盘(IRQ1)和串口(IRQ4)——住进了内核的
`kernel/char.c`:中断函数必须跑在"当前 cr3 指向的那份世界"里,而当时哪个任务
在跑说不好,所以碰端口、认 IRQ 的代码只能住进所有地址空间共有的内核(缘由在
[第 14 章](/guide/ring3)讲透)。输出端——VGA 和 COM1——对任何任务都只是
一个系统调用:`sys_write`。

于是 tty 成了纯粹的 ring 3 程序,一个端口都不碰,只认消息:

- 从内核收 `TTY_CHAR`:一次一个字符,来自键盘或串口;
- 对外只说三句话:打印(`TTY_PUTS`)、清屏(`TTY_CLEAR`)、要一行(`TTY_GETLINE`);
- 输出走 `sys_write`,屏幕怎么点亮的它不知道。

## 输入:中断变消息

内核里的中断函数短得不能再短——查表、变成字符、投一条消息:

```c
static void tty_char(char c) {
    struct msg m;
    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_CHAR;
    m.data[0] = c;
    ipc_send_kernel(TID_TTY, &m);  /* 内核寄的信,src=0 */
}
```

串口走同一条路:QEMU 把 `-serial stdio` 收到的字节交给同一个投递函数——所以
这台机器没有 GUI 键盘也能完整驱动。**中断函数只做三行**(读端口、解码、投递),
真正的逻辑全部留在能睡能醒的任务里;只是如今"等输入"不再是 tty 睡在 IRQ 上,
而是内核把字符直接送上门。

## 行协议:shell 要的是行,不是字符

shell 不想一个字符一个字符地处理退格。tty 把这些脏活揽下来:

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

`deliver` 检查:有没有 shell 正阻塞在 `TTY_GETLINE` 上?有,行一攒完就立刻
回消息;没有,把整行存进缓冲,等 shell 来要。**生产者和速度解耦**——你打字
再快,shell 再慢,行缓冲都兜着。

## 主循环:睡到内核送信来

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

先清空收件箱,再阻塞着睡——醒来第一件事是处理刚到的那条。这个"清完再睡"
的顺序和[第 3 章](/guide/interrupts)的教训同源:**永远不要带着没读的信入睡**。

## 回显

你敲的字符会出现在屏幕上,是因为 tty 收到后主动 `put(c)` 回显——不是硬件替它
做的。退格能抹掉字符,也是 tty 在 `\b` 时输出 `" \b"` 三个字符把光标推回去再抹。
终端体验的每一处细节,都是这个循环里的一行代码。

## 本章文件

```
kernel/char.c/.h     控制台硬件:COM1+PS/2 收进来的成消息,发出去的成系统调用
servers/tty/tty.h    消息类型(TTY_PUTS / TTY_GETLINE / TTY_CLEAR / TTY_CHAR)
servers/tty/tty.c    服务循环、行缓冲、应答 shell
```

下一章:[shell](/guide/shell)。
