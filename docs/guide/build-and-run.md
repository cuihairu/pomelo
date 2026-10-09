# 11 · 构建与运行

对应代码:`CMakeLists.txt`、`tools/mkfs/mkfs.c`、`tools/smoke.sh`

## 两条命令

```sh
cmake -B build && cmake --build build
qemu-system-i386 -kernel build/kernel -hda build/pomelo.img -serial stdio
```

第一条产出两个文件:`build/kernel`(内核)和 `build/pomelo.img`(磁盘镜像)。
第二条:QEMU 充当引导器加载 ELF,同时把镜像接到主 IDE 盘上,串口转发到当前终端。
`make run`(CMake 里的自定义目标)等价于第二条。

## 冒烟测试:CI 里跑的也是它

构建产物对不对,QEMU 说了算。`tools/smoke.sh` 开一台无显示的 QEMU,从串口
替你敲 `help`、`ls`、`cat hello.txt`,然后 grep 期望的输出:

```sh
ctest --test-dir build          # 或: cmake --build build -t test
```

测试是**节奏驱动**的:每条命令之间隔一秒——tty 一个中断才处理几个字符,
 burst 输入会撑爆它的行缓冲。给真机上电之前先让它自己跑一遍,这习惯值回票价。

## 交叉编译?不,本机编译就够

内核是**freestanding** 的:不用 libc,不链宿主库,所以不需要交叉工具链:

```cmake
set(KERNEL_FLAGS -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib)
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
add_custom_command(OUTPUT pomelo.img
    COMMAND mkfs pomelo.img ${CMAKE_SOURCE_DIR}/tools/mkfs/seed
    DEPENDS mkfs)
```

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

## 本章文件

```
CMakeLists.txt       顶层构建:内核、mkfs、镜像、run 目标、smoke 测试
tools/mkfs/mkfs.c    宿主机磁盘镜像生成器
tools/smoke.sh       QEMU 冒烟测试:串口驱动 shell,grep 验证
boot/kernel.ld       链接脚本
```

下一章:[扩展:MMU 与真隔离](/guide/beyond-mmu)。
