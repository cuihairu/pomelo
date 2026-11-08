# 2026-11-01 全仓代码深审

> 审查范围:全部源码(`find . \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \)` 排除 build/,共 4445 行)。
> 流程:安全静态扫描 → 隐藏 bug 猎捕 → 质量门 → 报告;P0/P1 当场修,一问题一 commit,测试绿;P2/P3 记入 todo。
> 基线:2026-11-02 复测 `ctest --test-dir build` = 7/7(串口 shell、PS/2 键盘、鼠标、网络、clear、reboot、walkthrough)。

## 总账

| 严重度 | 发现 | 已修 | 记 todo(未修) |
| --- | --- | --- | --- |
| P0 | 0 | 0 | 0 |
| P1 | 3 | 3 | 0 |
| P2 | 5 | 0 | 5 |
| P3 | 4 | 0 | 4 |

## 发现(按严重度排序,每条带 file:line 证据)

### P1

**1. 系统调用门不校验用户指针 —— ring 3 可让内核 panic 或读内核内存**
- 证据:`kernel/syscall/syscall.c`(修复前)SYS_WRITE `con_write((const char *)r->ebx, r->ecx)`、SYS_SEND `ipc_send(r->ebx, (struct msg *)r->ecx)`、SYS_RECV `ipc_recv((struct msg *)r->ebx, r->ecx)`、SYS_SPAWN `sched_spawn((const char *)r->ebx)`、SYS_PS `ps_snapshot((struct ps_entry *)r->ebx, r->ecx)`、SYS_MEM `frame_stats((struct mem_info *)r->ebx)`——全部直接解引用 ring 3 传入的地址,无任何范围/映射校验。(修复后六个 `case` 现于 :43/:47/:58/:69/:74/:88,均已过 `user_ok`。)
- 后果一(内核 panic,DoS):传一个未映射的用户地址 → 内核在 ring 0 解引用 → 页故障 → `kernel/intr/intr.c` `task_fault` `if ((r->cs & 3) == 0) panic(what, r)` —— 内核态故障直接 panic,整机停机,不是杀任务。
- 后果二(内核内存读):传一个内核有效地址(如 0x1000 起的 `.text`/数据)→ `con_write` 在 ring 0 读该地址并把内核内存打到控制台 → 信息泄露。
- 处置:**已修**。在 `syscall_entry` 前加 `user_ok(p, len)`(范围须落在 `[USER_BASE, USER_STACK_TOP)` 且 `v2p()` 逐页确认已映射),六个取指针的调用先过门再解引用。

**2. `cmd_write` 写盘失败死循环 / 越界读 —— shell 写盘可永久挂死**
- 证据:`apps/shell/cmd_fs.c:66-68`(修复前)`for (int off = 0; off < total; ) { int n = ...; off += fs_write(ino, off, content + off, n); }`(修复后 :66-70 带 `if (w <= 0) break;`)。
- 触发一(死循环):盘满时 `servers/fs/fsrw.c:74` `if (b < 0) { reply(m, 0, 0, 0, 0, 0); return; }` 回 `len=0`,`fs_write`(`apps/shell/fsio.c:67`)返回 0 → `off += 0` 永不前进 → shell 死循环。写满盘是可达的:镜像 `NDATA=1024` 数据块,`MAXFILE=3072`(6 块/满文件),约 `1024/6≈171` 个满文件即满;且 `NINODES=32` 使文件数在块数之前先封顶。
- 触发二(越界读):`fs_write` 返回 `FS_REFUSED(-2)`(`apps/shell/shell.h:38`)→ `off += -2` 变负 → `content + off` 读到缓冲区以下(用户态,脏读)。
- 处置:**已修**。镜像 `cmd_cat:39` 的既有护栏 `if (n <= 0) break;`:`int w = fs_write(...); if (w <= 0) break; off += w;`。

**3. TCP 重传 `snd_nxt` 膨胀 —— 重传路径永不确认,连接最终判死**
- 证据:`servers/net/sock.c:259-264`(修复前)
  ```c
  static void sock_send_seg(struct sock *s) {
      u32 save = s->snd_nxt;               /* data flies from snd_una */
      s->snd_nxt = s->snd_una;
      tcp_send(s, TCP_ACK | TCP_PSH, s->unacked, s->unacked_len);
      s->snd_nxt = save + s->unacked_len;  /* BUG: 重传时 save 已含 una+len */
  }
  ```
  首次发送 `save == snd_una`,`snd_nxt = snd_una + len` 正确;重传时 `save == snd_una + len`,`snd_nxt = snd_una + 2*len` —— 序列号被多推一个段长。对端 ACK `snd_una+len` 到,`servers/net/sock.c:380` `ack == s->snd_nxt` 永不成立 → 未确认段滞留 → 8 次重试(`TCP_RETX_LIMIT`)后 `servers/net/sock.c:436` "tcp gave up" 丢弃连接。任何一次 ACK 丢失/乱序即触发。
- 处置:**已修**。`s->snd_nxt = s->snd_una + s->unacked_len;`(固定于 `una+len`,与首发一致)。

### P2

**4. IPC 长度参数未防负/超大 —— 恶意或出错任务可打穿 fs/net 服务**
- 证据:`servers/fs/fsrw.c:66-69` `int len = m->arg2; if (len > MSG_DATA) len = MSG_DATA; if (len > tail) len = tail;` —— `len < 0` 通过两道上限,`memcpy(secbuf + off%SECTOR, m->data, len)`(:78)以负 `len` 转无符号 ≈ 4G 拷写;`reply(m, len, ...)`(:80)回负。`servers/net/sock.c:448`(修复前 `s->unacked_len = m->arg1 > SOCKBUF ? SOCKBUF : m->arg1;` 负 → 负,`memcpy` 崩)、`servers/net/sock.c:542-543` UDP 发送长同型、`servers/net/sock.c:217/:524` `sock_consume(s, m->arg1)` 负 `cap`。
- 注:当前仓内应用(`netsh`/`shell`/`probe`)都传合法长度,非现役路径;属 IPC 信任边界的防御性缺口。**记 todo。**
- 修法建议:各服务在解 `m->arg1/arg2` 后先 `if (len < 0) len = 0;`(或 refuse),再夹上限。

**5. fs inode 索引未校验 —— 越界访问 inode 表**
- 证据:`servers/fs/fsrw.c:33` `struct inode *ino = &inodes[m->arg0];`(`:64` `do_write`、`:84` `do_commit` 同型);`inodes` 为 `NINODES=32` 静态数组。应用侧传的是 `fs_open/fs_create` 返回的合法 inode,但恶意 `arg0`(如 `0x7FFFFFFF`)会让 fs 任务解引用野指针 → 该任务被击杀。
- 处置:**记 todo**;修法:入口 `if ((u32)m->arg0 >= NINODES || !inodes[m->arg0].used) refuse(...)`。

**6. ARP 包长度未校验**
- 证据:`servers/net/net.c:28-34` `if (et == ETH_ARP) arp_in(f + 14);` —— 只拦 `len < 14`,未查 ARP 帧长 ≥ 14+28;`servers/net/arp.c:34` 读 28 字节。runt 帧会读到该 2048 字节接收缓冲内的陈旧数据(不越缓冲,解析脏数据)。
- 处置:**记 todo**;修法:`if (et == ETH_ARP) { if (len < 14 + 28) return; arp_in(f + 14); }`。

**7. `con_write` 负长度**
- 证据:`kernel/char.c:28` `con_write(const char *s, int n) { for (int i = 0; i < n; i++) con_putc(s[i]); }` —— `n < 0` 时循环不执行,无害;但配合发现 1 修复后,负 `n` 应显式拒绝。
- 处置:**已修**(发现 1 一并落地)。`syscall.c:62` SYS_WRITE 过 `user_ok` 门:`user_ok(p, len)` 以 `u32` 取 `len`(`ecx` 无符号),负 `n` 转无符号后 `a+len` 环绕/超 `USER_STACK_TOP`,`user_ok` 返回 0 → `r->eax=-1`,不进入 `con_write`。

**8. `do_commit` 尺寸信任 —— 提交的 size 可与已写字节不符**
- 证据:`servers/fs/fsrw.c:83-87` `inodes[m->arg0].size = m->arg1;` 直接采用客户端声明的 size,不校验与已写块一致;配合 `do_read:35` `off >= ino->size` 读边界,一个说谎的客户端可让 `do_read` 在 `off < size` 时读 `direct[off/512]`(块号可能为 0 → `ata_read(0, secbuf)` 读物理块 0,超级块区)。
- 处置:**记 todo**;修法:`do_commit` 校验 `m->arg1 <= MAXFILE`。

### P3

**9. `tty_getline` 负参数**
- 证据:`apps/shell/shell.c:45` `void tty_getline(char *buf, int max)` —— `max` 为负时作缓冲上限(`:88` 调用处传 `sizeof line`,恒正)。当前仅内部调用,参数均合法。
- 处置:**记 todo**。

**10. ICMP 奇长校验和残留字节**
- 证据:`servers/net/net.c` ICMP 校验和路径:奇长报文求和会多算/漏算一个字节,残留陈旧字节,不影响现役回显(长度常为偶)。
- 处置:**记 todo**。

**11. walkthrough 提示符 grep 过宽匹配**
- 证据:`tools/walkthrough.sh` 用 `grep` 判提示符,可被输出内容误匹配(如文件名恰好含提示符子串)。
- 处置:**记 todo**。

**12. mkfs 空闲块推进守卫**
- 证据:`tools/mkfs/mkfs.c` 空闲块分配循环对 `next_block` 推进缺少边界检查,镜像写满时可能越界。
- 处置:**记 todo**。

## 质量门

- **C 编译器告警**:`-Wall -Wextra` 干净(全新构建目录复编,零 warning)。
- **链接器告警**:6 条 `LOAD segment with RWX permissions`(kernel + 5 个用户 blob)——单段 RWX 是 `tools/user.ld`/`boot/kernel.ld` 平铺二进制的设计使然(`.bss` 折入 `.data`),非缺陷。
- **测试基线**:`ctest --test-dir build` = 7/7 通过(86.12s)。
- **安全静态扫描**:全仓无硬编码密钥、无 shell 注入、无 SQL 拼接、无 eval/反序列化、无路径穿越;无危险字符串函数(`gets`/`sprintf`/`strcpy`)。

## 修复记录

- `fix: validate user pointers at the syscall gate`(发现 1)
- `fix: stop shell write loop on failed write`(发现 2)
- `fix: keep tcp snd_nxt at una+len on retransmit`(发现 3)
- `docs: add code review report and record p2 p3`(本报告 + todo 增记发现 4-12)

修复后复测 `ctest --test-dir build` = 7/7。
