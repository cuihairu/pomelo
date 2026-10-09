# 15 · 倒下与爬起

对应代码:`kernel/sched/prog.c`、`apps/shell/cmd_proc.c`

## 崩溃之后,是谁把它扶起来的

微kernel的招牌承诺只有一句话:服务崩了,内核不死,把那个服务单独重启一遍。
前面所有章节都在为此备料——每服务独立页表、内核只当跑腿的 IPC、故障现场杀一个
不留尸(死任务的栈永不再续)。这一章把最后一块装上:**按名字把程序再启动一次**。

## 注册表:能力清单,不是地址簿

内核镜像里嵌着四个 blob。启动时 `boot/main.c` 把它们登记进一张表:

```c
prog_add("fs",    _binary_ufs_bin_start,    _binary_ufs_bin_end,    EFLAGS_USER_IOPL);
prog_add("tty",   _binary_utty_bin_start,   _binary_utty_bin_end,   EFLAGS_USER);
prog_add("shell", _binary_ushell_bin_start, _binary_ushell_bin_end, EFLAGS_USER);
prog_add("probe", _binary_uprobe_bin_start, _binary_uprobe_bin_end, EFLAGS_USER);
```

这张表是**能力清单**:任务只能按名字请内核启动一个已注册的程序——给的是词,
不是地址,所以用户态永远没法让内核去映射任意内存。内核镜像本身就是可信来源,
注册表把"谁能被启动"钉死在编译期。

## 死服务回到自己的座位

`sched_spawn`(kernel/sched/prog.c)挑座位有一条规矩:**先找这个程序的老座位**。
任务表每个座位记着"谁住过这里",死掉的任务留名不留尸——座位空着,名字还在:

```c
for (int i = 1; i < NTASK; i++)     /* the old seat first */
    if (tasks[i].prog == p && tasks[i].state == ST_FREE) { slot = i; break; }
```

于是 fs 死在 1 号位,重启后还是 1 号:**TID 稳定,所有发给 TID_FS 的消息照走不误**。
"重启服务"因此是完整的恢复故事,而不是一次假装有过的重启。座位满员或名字不
存在,`spawn` 如实返回 -1,不多做什么。

出生流程因此只剩一条路:`boot/main.c` 也是先 `prog_add` 再 `sched_spawn`——
开机只是第一个拉幕的,和 shell 里敲的是同一扇门。

## 两面镜子:SYS_PS 和 SYS_SPAWN

系统调用表添了两行:

```c
SYS_SPAWN    = 7,   /* ebx=name: start a registered program */
SYS_PS       = 8,   /* ebx=buf, ecx=max: task-table snapshot */
```

`SYS_PS` 是只读的镜子:内核把任务表逐座抄给调用方,一行一个
(`struct ps_entry`:tid、状态、住过的程序名)。`SYS_SPAWN` 是唯一的门。shell
把它们包成两个命令:

```
pomelo> ps
tid program  state
 1 fs       blocked
 2 tty      blocked
 3 shell    ready
 4 probe    free
 5 -        free
pomelo> spawn probe
probe is task 4 now
pomelo> probe: alive at ring 3
pomelo> task 4 killed: page fault at 101000 (eip=40000055)
```

看这一屏:fs 和 tty 都 `blocked`——它们在 `sys_recv` 上睡大觉,等消息上门;
probe 是 `free`——开机时撞墙被它自己作死的那次,座位空着;`spawn probe` 把
它扶回 4 号位,它再喊一遍"我还活着",再撞一次墙。**倒下、爬起、再倒下**,全程
没有重启机器,没有动内核一行状态。smoke 和 nightly 把这三行(两遍 alive、两遍
kill)列为必查项。

## 顺带修好的一处并发

`frame_alloc` 从前裸奔:没有任何关中断保护。以前没人并发用它,相安无事;
`spawn` 可以从 shell 的系统调用里发起,而时钟随时可能在系统调用中间切走 CPU
——两个任务同时走进 `pdir_user_new`,同一帧内存就会被判给两个世界。现在分配
器内外一对 `intr_disable/intr_enable`,座位认领(`task_start`)也关着中断
做——"检查与占坑必须一口气做完",和[第 4 章](/guide/scheduling)阻塞的原
则同源。

## 本章文件

```
kernel/sched/prog.c     注册表、sched_spawn、ps_snapshot
kernel/syscall/syscall.h SYS_SPAWN / SYS_PS 与包装
apps/shell/cmd_proc.c   ps 与 spawn 命令
```

下一章没有新的了。把[构建与运行](/guide/build-and-run)里的 nightly 截图跑
一遍,盯着 `ps` 的输出改一行状态,再 `spawn` 一个回去——微内核的容错故事,
你已经亲手演过一遍了。
