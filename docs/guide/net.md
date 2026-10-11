# 16 · 网络栈与 socket 服务

对应代码:`servers/net/net.c`、`apps/netsh/netsh.c`

## 网卡驱动住在用户态

[第 8 章](/guide/fs-server)立过一条规矩:ATA 端口归 fs 任务,内核只递消息。
网卡照办——e1000 的 PCI 配置空间、MMIO 寄存器、收发描述符环,整个驱动都
住在 ring 3 的 net 任务里。fs 靠出生时的 IOPL=3 摸到端口,net 靠同一份
特权摸到 PCI(见 `boot/main.c` 的 `EFLAGS_USER_IOPL`);内核出面的只有
中断这条线:`SYS_IRQ_ENABLE` 放行 PIC 上的某脚,中断来了用 `irq_wait`
叫醒任务,和[第 9 章](/guide/tty-server)的键盘一个待遇。

MMIO 是新面孔。设备寄存器映在物理地址上,任务先 `SYS_MMIO` 换来一个
虚拟地址,再像读写内存一样读写寄存器;描述符环要给 DMA 用,拿
`SYS_V2P` 把虚拟地址翻译成网卡认的物理地址。内核对网卡的了解到此为止:
它不知道 e1000 是什么,只知道有个任务在等某条中断线。

收发都是描述符环:一串定长的"信封",每封装一个缓冲区地址。网卡收完一包
把状态写回信封,把中断线拉一下;驱动扫一遍环,取走包,把信封重新挂回去。
没有队列溢出的聪明策略——环满了就丢,网卡和驱动都不装作没看见。

## 四层,每层一个函数

net.c 里协议栈就是一条调用链:`net_rx` 认以太网类型号,ARP 的包给
`arp_in`,IP 的给 `ip_in`;`ip_in` 认协议号,ICMP 回显请求就地应答,
UDP 和 TCP 的包按目的端口找 socket,交给 `sock_in_udp` / `sock_in_tcp`。
ARP 先问网关的 MAC,ICMP 跟网关打一个来回,这两件事在启动时各做一次,
串口上有字,pcap 里有帧,可信可查。

校验和值得单独说。UDP 和 TCP 的校验和都盖着一个"伪首部"——源目 IP、
协议号、长度拼在数据前面一起算(rfc 768 / 793)。两个协议共用同一个
`pseudo_cksum`,差别只是协议号参数。这个参数一开始写死成 UDP 的,结果
TCP 段全部被对端默默丢弃——主机不回话,pcap 里只有重发的 SYN。教训记在
这里:**两个协议长得像,不等于一个常数能同时伺候它们。**

## TCP:一个小得诚实的状态机

栈里的 TCP 只有四个状态:LISTEN、SYN_SENT、SYN_RCVD、ESTABLISHED。没有
选项协商,没有 SACK,没有拥塞控制,不收乱序段,固定初始序号,固定窗口,
一个 socket 一次一个连接。这些缺口写在代码的注释里,不藏在设计文档里:
对端是宿主机上的测试工具,段按序到达,乱序了就发重复 ACK 讨重传——
**够用的定义取决于对端是谁,这个栈知道自己伺候的是谁。**

靠得住的部分是重传。发出去没被确认的段挂 50 个心跳(约半秒)重发一次,
八次不回就写掉这条连接,唤醒所有挂在它上面的调用者。FIN 也走同一条路:
close 时数据还没确认完,就等重传心跳把线路清空再发 FIN——**拆连接和发
数据抢同一条线时,排队,不插队。**

```c
if (!s->unacked_len) {           /* only a parked close remains */
    tcp_send(s, TCP_ACK | TCP_FIN, 0, 0);
    sock_drop(s);
    continue;
}
```

## socket:把协议栈变成服务

到上一节为止都是"一个任务自己聊得欢"。socket 这层把栈开放出去:net
任务持一张四座的 socket 表,app 按消息说话——

| 消息 | 请求 | 回答 |
| --- | --- | --- |
| OPEN | 协议、本地端口、对端(全零 = 监听) | arg0=socket,arg1=本地端口 |
| ACCEPT | socket | arg0=socket,data=对端 ip+端口 |
| RECV | socket、上限 | arg0=长度,arg1=-1 表对端结束 |
| SEND | socket、长度、数据 | arg0=已发长度 |
| CLOSE | socket | 无回答 |

每条请求一个回答,沿用 fs 的旧约:同型,arg0 带数,负数是否。两条新约
是这一章逼出来的:OPEN 的回答带 arg1(本地端口),因为一个 app 会同时
开几个 socket,答案回来不分先后,**得让答案自己说清是哪个**;RECV 和
SEND 的回答带 arg2(socket 号),理由相同——答案先到,请求的名字早忘了,
猜是要出事的。

RECV 和 ACCEPT 没有"没数据,过会儿再问"的返回:调用者连消息一起睡在
socket 上,流量到了才被唤醒。park 的写法和内核的 irq 等待队列同款——
每个 socket 只有一个座位,第二个 RECV 上门会被直说"已经有人在这儿了"。
没有超时:对端不来,调用者就一直睡。这个缺口是故意留的,注释里写着。

主动打开只认网关。`ip_tx` 的 ARP 表里只有一个名字,发给别人的包会
无声无息地消失,所以 OPEN 收到网关以外的地址直接拒绝,拒绝理由原样
送到:"only the gateway is reachable"。**限制被说出来,才是限制;
藏起来,是坑。**

## netsh:第一个客户

netsh 是个事件循环:开机发三个 OPEN——一条出站 TCP 去宿主机的 2324
送问候,一条监听 2323 回显 TCP,一条 2325 回显 UDP——然后原地等回答,
来什么处理什么。回显就是把 RECV 收到的字节 SEND 回去;问候发完即 CLOSE。

两个细节是调试换来的。其一,这个栈的监听者在第一个 SYN 到来时就变身成
那条连接(一个 socket 一个连接),所以每条连接结束,netsh 重开一个新
监听补位,而不是对着旧 socket 再 ACCEPT。其二,UDP 没有"对端结束"这
回事,回显的收发循环按 RECV 回答里的 socket 号分流,不靠"上一个
RECV 是发给谁的"去猜——**事件循环里,凡是靠记忆的归属,迟早张冠李戴。**

## 验证:nc 就是验收

宿主机上三句话:

```sh
nc -l -p 2324 &                              # 等问候
printf 'hello net\n' | nc -N 127.0.0.1 2323  # TCP 回显,回车即回
printf 'ping udp\n'  | nc -u -w2 127.0.0.1 2325
```

`-N` 不是摆设:stdin 到 EOF 后 nc 要半关写端,内核回 FIN、nc 回应答,
连接才算走完;不带 `-N` 的 nc 会一直等,等到的是超时,不是内核的错。
串口上应能看到 `netsh: tcp echo on 2323` 与每个连接的 `netsh: peer
closed`。空手也能试:不给 2324 放监听者,问候的连接重试八次后放弃,
串口打印 `net: connect gave up`——**失败要出声,是这个内核的规矩。**

## 本章文件

```
servers/net/net.c   e1000 驱动、ARP/IP/ICMP/UDP、最小 TCP、socket 表与消息应答
servers/net/net.h   消息协议、e1000 寄存器、以太网与 ip 常量
apps/netsh/netsh.c  事件循环:双回显 + 出站问候
kernel/string.c     memmove:socket 接收缓冲的压实
```
