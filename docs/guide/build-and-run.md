> **中文** | [English](build-and-run.en.md)

# 11 · 构建与运行

对应代码:`CMakeLists.txt`、`tools/mkfs/mkfs.c`、`tools/smoke.sh`

## 安装 QEMU

QEMU 扮演那台 i386 机器,是唯一的运行依赖。装法:

| 平台 | 命令 |
| --- | --- |
| Ubuntu / Debian | `sudo apt install qemu-system-x86 qemu-utils` |
| Fedora / CentOS | `sudo dnf install qemu-system-x86` |
| macOS | `brew install qemu` |
| Windows | `winget install qemu`,或从 qemu.org 下载单文件安装包 |

装完验证:

```sh
qemu-system-i386 --version
```

## 两条命令

```sh
cmake -B build && cmake --build build
qemu-system-i386 -kernel build/kernel -hda build/pomelo.img -serial stdio
```

第一条产出两个文件:`build/kernel`(内核)和 `build/pomelo.img`(磁盘镜像)。
第二条:QEMU 充当引导器加载 ELF,同时把镜像接到主 IDE 盘上,串口转发到当前终端。
`make run`(CMake 里的自定义目标)等价于第二条。想看真实的启动画面,去掉
`-serial stdio` 即可:QEMU 开一个窗口,键盘输入走模拟 PS/2,和串口是两条路。

命令里没写机型,默认就是 `pc`——主 IDE 盘挂在这台机器的 PIIX3 上。别特意加
`-M q35`:那台机器的盘走 AHCI,ATA 端口悬空,fs 服务会打印一行提示然后停在
原地。这是设计好的降级,不是卡死,见[第 8 章](/guide/fs-server)的 ata.c。

## 网络:一块 e1000,挂在用户态网上

`make run` 和两份冒烟脚本给 QEMU 加了三个参数:

```sh
-netdev user,id=n0,hostfwd=tcp::2323-:2323 \
-device e1000,netdev=n0 \
-object filter-dump,id=f0,netdev=n0,file=net.pcap
```

- `-netdev user`:qemu 内置的 SLIRP 用户态网,不需要 root、不碰宿主网卡,
  一切都在 qemu 进程里仿真。地址是约定死的:guest 拿 10.0.2.15,网关
  10.0.2.2;`hostfwd=tcp::2323-:2323` 把宿主的 2323 端口转进 guest 的 2323,
  给后面章节的 netsh 用。
- `-device e1000`:插一块 82540EM 网卡。net 任务开机探测它、读 MAC、
  挂好收发环,发一个 ARP 请求——“net: e1000 up”那行和 pcap 里第一帧,
  就是网线通了的全部证据。
- `-object filter-dump`:把这块网卡上的帧原样落进 `net.pcap`,宿主机
  `tcpdump -nn -r net.pcap` 就能逐帧检查。SLIRP 的帧不出宿主网卡,抓包
  只能走这个文件,不能靠 tcpdump 监听真实接口。

## 冒烟测试:CI 里跑的也是它

构建产物对不对,QEMU 说了算。`tools/smoke.sh` 开一台无显示的 QEMU,从串口
替你敲一串命令(`help`、`ps`、`spawn probe`、`ls`、`cat hello.txt`、`uptime`、
`echo`、`mem`),然后逐条判分——启动横幅、e1000 上线、netsh 的监听通告、
每条命令的答复、help 清单与命令表的对表、probe 的两死两生,
任何一处不对,整轮就算失败;
`tools/walkthrough.sh` 走得更远——
多一整套 `write`/`ls`/`cat` 的新建文件往返,是 nightly 每晚跑的那份完整走查:

```sh
ctest --test-dir build          # 或: cmake --build build -t test
```

测试是**节奏驱动**的:每条命令之间隔一秒——tty 一个中断才处理几个字符,
burst 输入会撑爆它的行缓冲。给真机上电之前先让它自己跑一遍,这习惯值回票价。

`tools/smoke-ps2.sh` 测的是另一条输入路:串口挂成 `null`,谁也不喂它,
QEMU 的 `sendkey` 在模拟 PS/2 键盘上敲一个 `mem`,再从 `0xb8000` 的 VGA
文本缓冲里把应答抠出来——键盘中断、扫描码表、屏幕回显,全在这条测试里。
`tools/smoke-mouse.sh` 把同一套取证搬到 PS/2 鼠标上:monitor 发
`mouse_move` / `mouse_button`,验的是光标格的属性字节(`0x70` 是光标,
`0x07` 是它离开后还原的格子)。
`tools/smoke-net.sh` 轮到网络:它自己开一台带 `hostfwd` 的 QEMU(宿主机的
2323/2325 进得来),先在宿主机上竖起一只 `nc -l -p 2324` 的耳朵(openbsd
变体,`-N` 发完即半关,不然回显等不来 EOF),再验四样——两条 TCP 回显,
第二条专门压“连接结束、重开监听”的变身路;一条 UDP 回显;问候语真的到了
宿主机;两条 TCP 各数到一次 `peer closed`。这只耳朵也是血泪:上一轮失败
残留的 `nc` 会把下一轮的问候抢走,于是 trap 里 QEMU 和耳朵同生共死——
清理自己,是测试的一部分。

命令电池里还有两条不吭声的,各配一个测试,证的是它们**不说话**这件事:
`tools/smoke-clear.sh` 敲一个 `mem`,把 `0xb8000` 拍下来,再敲 `clear` 拍第二张——
第二张图上只剩重打出来的提示符,启动横幅和刚才那条应答都不见了;`clear` 在串口上
一个字都不吐(它是发给 tty 的消息,tty 回一句 `SYS_CLEAR` 就把显存擦了),
所以证据只能是“擦干净了”和“擦完 tty 还活着”。
`tools/smoke-reboot.sh` 更干脆:QEMU 带 `-no-reboot` 起,敲完 `reboot` 就该自己
退出(状态 0);要是一直坐到 `timeout` 把它掐死(状态 124),就是那一拍
`0x64 <- 0xfe` 根本没把机器放倒。串口记录只能证明那句 `rebooting...` 打出来了,
机器到底停没停,看的是退出状态。

## nightly:每晚真跑一遍

冒烟测试只跑 `pc`。仓库的 [nightly workflow](
https://github.com/cuihairu/pomelo/actions/workflows/nightly.yml)每晚做一轮更
完整的取证:构建内核、镜像和 ISO,然后 **q35 与 pc 两台机器各开机取证一遍**——
先跑 `tools/walkthrough.sh` 走完整命令电池、抓串口记录,再用 monitor 的
`screendump` 抓启动画面——把 ISO、内核、磁盘镜像,连同两台机器各自的
`serial-q35.log`、`boot-q35.png`、`serial-pc.log`、`boot-pc.png`,一起挂到
rolling 的 [nightly release](
https://github.com/cuihairu/pomelo/releases/tag/nightly)。产物名必须带机器号:
同一条上传里出现两个同名资产,GitHub 直接 422。触发方式有两种:每晚定时,或
手动 `workflow_dispatch`(可选附加 QEMU 参数,例如 `-d int` 看中断)。
取证脚本是 `tools/nightly-run.sh`,机型差异的判绿口径写在文件头,q35 允许只过
shell 检查,挂死一律算失败;走查的判决写在脚本末段:**走查的判决就是当晚的
判决**——walkthrough 判分不过,nightly-run 以失败退出,两臂不齐绿,刷 release 的
一步根本不会执行。

## 交叉编译?不,本机编译就够

内核是**freestanding** 的:不用 libc,不链宿主库,所以不需要交叉工具链:

```cmake
target_compile_options(kernel PRIVATE
    -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin
    -nostdlib -Wall -Wextra -O2 -g)
```

- `-m32`:生成 32 位代码(QEMU pc 机器是 32 位引导);
- `-ffreestanding`:别假设有标准库;
- `-fno-pie` / `-fno-stack-protector`:关掉宿主发行版默认的地址随机化和栈保护,
  它们都需要运行时支持,内核里没有。

链接用自带的 `boot/kernel.ld` 把内核放到物理地址 1MB 处,并保证 multiboot 魔数
在文件最前面——QEMU 只认开头的 8KB 里有没有那个魔数。

## mkfs:在宿主机上铺一块盘

`tools/mkfs/mkfs.c` 是普通 Linux 程序,和内核毫无关系,只是按[第 7 章](/guide/disk-format)
的格式写文件:

```
  写超级块(1 扇区)
  写 inode 表(4 扇区,全零)
  逐个写入种子文件:分配数据块 → 填充 → 填 inode
```

预置的两个文件:`/hello.txt`(给 `cat` 用)和 `/readme.md`(给 `ls` 用)。
构建时 CMake 先编 mkfs、再跑它生成镜像,顺序依赖写在同一条命令里:

```cmake
add_custom_command(OUTPUT ${IMG}
    COMMAND $<TARGET_FILE:mkfs> ${IMG} ${CMAKE_SOURCE_DIR}/tools/mkfs/seed
    DEPENDS mkfs)
```

(`${IMG}` 就是 `build/pomelo.img`;`$<TARGET_FILE:…>` 是 CMake 的生成器
表达式,展开成 mkfs 编译产物的真实路径——引用目标,不写死路径。)

## 为什么镜像和内核是两个文件

内核经由 `-kernel` 进内存,镜像经由 `-hda` 挂盘,两者互不知晓。这正好复刻了
真实系统的分工:引导器负责代码,块设备负责数据。想换镜像内容,不用重编内核;
想改内核,镜像纹丝不动。

## 常见问题

| 症状 | 原因 |
| --- | --- |
| 开机即重启 | 中断开早了,见[第 2 章](/guide/boot) |
| `fs: bad disk` | 没挂 `-hda` 或镜像损坏,重新 `cmake --build build` |
| 键盘没反应 | 看串口有没有输出,确认 tty 任务被创建了 |
| 鼠标没反应 | 串口里 `mouse:` 那行是初始化结论:没这行是 8042 握手没过,有这行还没动,多半是 `-usbdevice tablet` 之类的参数把默认 PS/2 鼠标顶掉了,IRQ12 自然不来事件 |

## 本章文件

```
CMakeLists.txt         顶层构建:内核、mkfs、镜像、ISO、run 目标、smoke 测试
tools/mkfs/mkfs.c      宿主机磁盘镜像生成器
tools/smoke.sh         QEMU 冒烟测试:串口驱动 shell,grep 验证
tools/walkthrough.sh   实机走查:完整命令电池 + write/ls/cat 往返,逐条判分
tools/smoke-ps2.sh     QEMU 冒烟测试:PS/2 键盘输入,VGA 文本缓冲取证
tools/smoke-mouse.sh   QEMU 冒烟测试:PS/2 鼠标事件,VGA 属性字节取证
tools/smoke-net.sh     QEMU 冒烟测试:hostfwd 进出,socket 服务对 nc 说话
tools/smoke-clear.sh   QEMU 冒烟测试:clear 擦屏前后各拍一张 VGA,擦干净才算过
tools/smoke-reboot.sh  QEMU 冒烟测试:reboot 之后 QEMU 自己退出(状态 0)才算过
tools/nightly-run.sh   nightly 取证:双开机,串口记录 + screendump
tools/mkiso.sh         把内核打成 BIOS 可引导的 ISO(grub multiboot)
boot/kernel.ld         链接脚本
```

下一章:[PS/2 鼠标](/guide/mouse)。
