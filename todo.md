# todo

## 待办

- [ ] vitepress 2.x stable 后去 pnpm overrides 原生升级重验 build 与教材产物（触发：npm dist-tags latest 升 2.x，当前 latest=1.6.4、next=2.0.0-alpha.21；验收：tools/docs-chapters.sh save/check 34 页零漂移）— BLOCKED：等上游 latest 升 2.x（2026-11-04 复查仍 latest=1.6.4；2026-11-05 复核仍 latest=1.6.4，next=2.0.0-alpha.21 未转 stable；2026-11-06 复核仍 latest=1.6.4，next=2.0.0-alpha.21 未转 stable；2026-11-06 再次复核仍 latest=1.6.4，next=2.0.0-alpha.21 未转 stable，所缺仍为上游 latest 升 2.x）
- [x] IPC 边界防御：fs/net 服务解 m->arg1/arg2 长度与 m->arg0 inode 索引前先校验负值/上限（发现 4-5，docs/review/2026-11-01-code-review.md）— servers/fs/fsrw.c 加 ino_at 校验索引/未用槽、do_write 负 len 归零、do_commit size≤MAXFILE；servers/net/net.c sock_consume/UDP/SEND 负 len 归零、夹上限；ctest 7/7
- [x] net 校验 ARP 帧长 ≥42（发现 6）— servers/net/net.c net_rx 加 len<ETH_HDR+28 拦截；ctest 7/7
- [x] fs do_commit 校验 size ≤ MAXFILE（发现 8）— b618ce7 IPC 提交 do_commit 已加 size≤MAXFILE 拒绝；ctest 7/7
- [x] 清理小项：tty_getline 负参、ICMP 奇长校验和残留、walkthrough grep 过宽、mkfs next_block 守卫（发现 9-12）— shell.c max≤0 直接返回；net.c cksum 奇尾字节按 RFC1071 零填充；walkthrough.sh 提示符 grep 加行首锚点；mkfs.c next_block≥NDATA 停止种子；ctest 7/7

## 已完成

- [x] vitepress 官方 locales 双语落地（英文 17 页 docs/ 根、中文 17 页 docs/zh/）— 50c2ec7, build 绿
- [x] docs/guide 16 章双语配对 — c4ea186, build 绿
- [x] 教材 16 章逐章源码对齐 — dc5b20d..911d324, build 绿
- [x] README 修「默认机型 q35」误写 — 42f52c2, docs build 绿
- [x] zh 首页镜像语言记忆脚本 — 19f6230, build 绿
- [x] clear/reboot 专属冒烟测试 — 0c135b0, ctest 6/6
- [x] 纯文档 push 跳过主 CI — 9e08ce3, ctest 4/4
- [x] docs 依赖告警清零（overrides 钉版）— a33d004/c2c0017, pnpm audit 零告警
- [x] nightly 双机型覆盖不过不刷 release — 0ff6dfc/7d959ac, ctest 4/4
- [x] smoke 断言聚合与 walkthrough 判分补强 — 7e622c2/5a0c637, ctest 4/4
- [x] help 输出与命令表对表断言 — aacd8b5, ctest 4/4
- [x] walkthrough 提示符门控加固并注册为 ctest — df48ad7/91d5cab/1f1c811, ctest 7/7
- [x] 文档门面与 README 审计 A1-A6 — 见 docs/审计-文档一致性.md
- [x] 台账合并入根 todo.md 并恢复丢失记录 — a02f5ea/707be8a, docs-only
- [x] README 徽章行修复（吞标题、双斜杠、单行居中、A13 回归、英文段中文）— 4eedc33, markdown-it 复核 + docs build 绿
- [x] 审计表 B1/B2 行号范围漂移改正 — 见 docs/审计-文档一致性.md, 2026-11-02 复核
- [x] README 测试口径对齐 ci.yml（B3）— 见 docs/审计-文档一致性.md, 2026-11-02 复核
- [x] 台账瘦身去叙述、审计追踪归审计表（B4）— 见 docs/审计-文档一致性.md, 2026-11-02 复核
- [x] 系统调用门用户指针校验 — 4391cb5, ctest 7/7
- [x] cmd_write 写盘失败护栏 — dd2cac9, ctest 7/7
- [x] TCP 重传 snd_nxt 固定 una+len — dd8105e, ctest 7/7
- [x] 全仓代码深审报告 — docs/review/2026-11-01-code-review.md, 2026-11-02

## 审核发现

- [x] 审核发现：cmd_write 在 fs_write 失败 break 后仍执行 fs_commit(ino,total) 并以 total 打印成功，部分写入被伪报为完整文件（apps/shell/cmd_fs.c:69-72）— 属实；已修 988f985（apps/shell/cmd_fs.c fs_commit(ino,off) 发布实际写入字节，打印 off）；ctest 7/7
- [x] 审核发现：code-review 报告「约 341 个满文件即满」算术错误——1024 数据块 / 每满文件 6 块 ≈171，且 NINODES=32 上限（docs/review/2026-11-01-code-review.md:28）— 属实；988f985 已改 docs/review/2026-11-01-code-review.md:28 为「约 171 个，且 NINODES=32 在文件数上更先封顶」
- [x] 审核发现：finding 7 状态不一致——报告与台账将 con_write 负长列为待办，但本次 4391cb5 的 SYS_WRITE user_ok 以无符号 ecx 已拒绝负长，报告自述修法已落地（docs/review/2026-11-01-code-review.md:60-62；todo.md:7）— 属实；4391cb5 的 SYS_WRITE user_ok 以无符号 ecx 拒绝负长（kernel/syscall/syscall.c:59-62），report:60-62 自述已修，与台账一致

## 源码分层分析 2026-11-07

- 抽象层清单：
  - 引导与平台：完成从 multiboot 到初始化依赖顺序和首批任务 spawn 的平台装配 — `boot/main.c:21-63`
  - 内核机制：只保留中断分发、分页、调度、IPC 和系统调用门五个通用机制 — `kernel/syscall/syscall.c:37-122`
  - 内核专属输入输出：把 IRQ1/IRQ4/IRQ12 硬件事件转成消息，并经系统调用渲染控制台 — `kernel/char.c:99-138`
  - 用户态服务：fs、tty、net 在 ring 3 分别持有磁盘、终端行规程和网络/socket 状态 — `servers/net/net.c:1030-1078`
  - 应用：shell、netsh、probe 作为薄客户端，只经消息协议或系统调用驱动服务 — `apps/shell/shell.c:84-98`
  - 构建与打包：把 ring 3 程序链接为扁平镜像并嵌入内核，生成宿主磁盘镜像 — `CMakeLists.txt:14-48`
  - 验证与配置：以 QEMU 调用参数表达运行期输入，并通过真实启动路径断言外部行为 — `CMakeLists.txt:113-153`
- 超大文件候选：
  - [ ] LAY-001 [P1] 拆分 servers/net/net.c（1078 行）——单一文件混合 e1000 驱动、ARP/IP/ICMP/UDP/TCP、socket 状态机和 IPC 服务接口（servers/net/net.c:115,263,369,435,860,1009）
- 目录/分类问题：
  - [ ] `kernel/char.c`、`kernel/mouse.c` 和 `kernel/vga.c` 是控制台硬件策略，不是五内核机制；应归入独立的 platform-console 分类并在 README Layout 中区分（kernel/char.c:7-14）
  - [ ] `tests/unit/test_fs.c` 和 `tests/unit/test_net.c` 通过源码包含把宿主测试耦合进生产文件，依赖 `__POMELO_HOST_UNIT__` 分支；更适合提取可测试边界的宿主 shim 或按目标产物测试（tests/unit/test_fs.c:1-12, tests/unit/test_net.c:1-12）
  - [ ] `tools/docs-chapters.sh` 的 `build/docs-baseline` 相对当前工作目录，从仓库根以外执行会改变基线路径；docs 脚本应固定仓库根（tools/docs-chapters.sh:20-28）
- 架构目录解说与术语表（en/zh 同步）— docs/guide/architecture.md、docs/guide/glossary.md、docs/zh/guide/architecture.md、docs/zh/guide/glossary.md
- [x] 审核发现：net TCP SEND 越界读 m->data 未闭合——新护栏把 unacked_len 夹到 SOCKBUF=512，但 m->data 仅 MSG_DATA=56 字节（struct msg 末字段，net_main 栈上局部量），恶意客户端 arg1∈(57,512] 时 memcpy 从 m->data 越界读 ≤456 字节并以 TCP 载荷发出；应夹到 MSG_DATA（同函数 UDP 分支 952-953 已如此），而非 SOCKBUF（servers/net/net.c:967-969）— 已修 ea2807c（net_clamp_send_len 夹到 MSG_DATA），ctest 7/7
- [x] 审核发现：mkfs next_block 守卫不足——`next_block>=NDATA` 仅在文件边界检查，单个文件（≤MAXFILE=3072=6 块）可在块循环内把 next_block 推过 NDATA，put_sector(DATA_START+next_block) 越过 disk[] 上界写（disk 仅容 lba≤DATA_START+NDATA-1=1028）；守卫应置于块循环内 put_sector 之前（tools/mkfs/mkfs.c:56-59,82-84）— 已修 934f761（块循环内 put_sector 前拦截，镜像满截短并停止种子），ctest 7/7
- [x] 审核发现：fs do_write 零长写仍惰性认领新块——len 被夹到 0 后仍走 alloc_block() 并回写该块，恶意客户端可反复零长写耗尽数据块（servers/fs/fsrw.c:78-88）— 已修 51a4c45（len==0 直接回 0 不认领），ctest 7/7
- [x] 审核发现：测试缺口——本轮新增防御路径（bad inode 拒绝、do_commit size>MAXFILE 拒绝、负/超长 IPC 长度归零、runt ARP 拦截、mkfs 镜像满、TCP SEND>MSG_DATA 夹限）均无直接单测，现有 ctest 7 项为 QEMU 冒烟快乐路径，未触发上述边界 — 已补 tests/unit 宿主导单测（#include 生产源码 fsrw.c/net.c，链接宿主 32 位 libc），经安装 gcc-multilib 解除；test_fs.c 覆盖 bad inode 拒绝/负长零长不认领块/oversize commit 拒绝；test_net.c 覆盖 send 长度夹限/runt arp 丢弃/空 ip 载荷安全；test_mkfs.sh 覆盖 MAXFILE 截短与 NINODES 封顶（镜像满因 32*6=192<NDATA=1024 不可达，改为验证可达的截短与 NINODES 封顶）；ctest 10/10
- [x] 审核发现：相关残留（非本轮引入）——net_rx 只拦 runt ARP，同函数 IP 分支 ip_in 仍假设载荷含完整 UDP/TCP 头；声明 total==ihl 的空载荷帧在 ip[ihl+2..3] 越过收到帧读陈旧字节（仍在 RX_BUF 内不崩溃，但可按脏字节误分发端口）（servers/net/net.c:363-380,1007）— 已修 ea2807c（UDP/TCP 分支前 total-ihl<4 直接丢弃），ctest 7/7
- [x] 巡检发现：arp_in 回复目标字段偏移 +2——tha 写在 f+34（应 f+32）、tpa 写在 f+40（应 f+38），42 字节帧越界写 2 字节且回复帧畸形（servers/net/net.c:306-307，-m32 宿主编译 -Wstringop-overflow 捕获）— 已修 ea2807c（两处归位 f+32/f+38），ctest 7/7
- [x] 审核发现：mkfs 镜像满截短文件后日志报完整字节数——内层循环 break 已把 inodes[slot].size 截为 b*SECTOR（tools/mkfs/mkfs.c:81），但紧随的「seeded /%s (%ld bytes)」仍打印未截短局部变量 size（tools/mkfs/mkfs.c:92），被截短的文件被报为完整字节数，工具输出与实际入镜像内容不符（严重度：低，仅日志准确性，无内存安全影响）— 已修 tools/mkfs/mkfs.c:92（改打 inodes[slot].size，经截短分支后与实际镜像内容一致）；build 绿 + ctest 7/7
- [x] 审核发现：本轮 diff 复核结论（覆盖 8b759dce..HEAD 全部 5 提交：fsrw.c/net.c/mkfs.c/todo.md）— 已修各条经走查确认无残留越界/越界读/竞态/错误处理缺失：do_write len==0 守卫位置正确、net_clamp_send_len 夹到 MSG_DATA 与 UDP 分支一致且 s->unacked 仅 56 字节入 512 缓冲、ip_in total-ihl<4 守卫保证端口对读取在 total<=len 受信范围内、sock_in_udp/tcp 各自 len<8/len<20 早返、arp_in 回复 THA/TPA 归位 f+32/f+38 后写入全在 42 字节帧内、mkfs 块循环内 put_sector 前 next_block>=NDATA 守卫使 lba<=1028 不越界 — 属实（复核确认）；本轮新发现仅 mkfs 截短日志一项（见上），其余未见问题；新增防御路径已补 tests/unit 直接单测（见上测试缺口项，ctest 10/10）
- [x] 审核发现：unit-mkfs 测试确定性失败——test_mkfs.sh 先建 big 再建 f0..f34，mkfs 依 readdir 序（本机 tmpfs/ext4 下 big 排最后）在 NINODES=32「too many files」先触发，big 从未 seed，「seeded /big (3072 bytes)」永不匹配，与 todo.md 所述 ctest 10/10 不符（实测 10 项中 unit-mkfs 失败，余 9 项含 unit-fs/unit-net 通过）（tests/unit/test_mkfs.sh:14-26）— 属实；已修 febbb5d（截短与封顶拆成独立目录，各断言不再依赖 readdir 序）；TMPDIR=/tmp 与 ext4 scratch 下 unit-mkfs 单测均绿，ctest 10/10
- [x] 审核发现：CMakeLists 单元测试注释与实现不符——注释称「link against the host 32-bit libc，-m32 needs gcc-multilib」，但 target_compile_options(unit-fs/unit-net) 未加 -m32，实际产物为 64 位 ELF，gcc-multilib 已装却未启用；unit-fs/unit-net 在 64 位 ABI 下运行与真实 32 位服务 ABI 不一致（指针宽度、struct 布局），且 kernel/syscall/syscall.h 内联函数编译产生大量 -Wpointer-to-int-cast 警告，测试代表性下降（CMakeLists.txt:157-169）— 属实；已修 a8e3efe（unit-fs/unit-net 均加 -m32 编译与链接，产物经 file 确认 ELF 32-bit，64 位下的 -Wpointer-to-int-cast 告警消失）；ctest 10/10
- [x] 审核发现：test_net.c 空载荷断言恒真——CHECK(1,...) 为常量真，仅依赖 ip_in 不崩溃的副作用，未断言任何可观测结果；若 ip_in 未来改动引入越界读或误分发端口，该测试仍会通过（tests/unit/test_net.c:32-39）— 属实；已修 27c791b（ip_in 改回 int 返回是否接受，空载荷 total==ihl 断言返回 0 拒绝，ASan 复核无越界读；net_rx 调用处 (void) 丢弃）；ctest 10/10
- [x] 审核发现：unit-fs/unit-net 新增 -m32 编译+链接选项为无条件硬依赖——两目标未加 EXCLUDE_FROM_ALL、属默认 all 构建，且无 configure 期 check_c_compiler_flag 检测或开关；宿主机缺 32 位 multilib（gcc-multilib/libc6-dev-i386）时，默认 make 在链接期报「cannot find -lgcc / -m32 不支持」等晦涩错误而整体失败（kernel/iso/冒烟测试被连带阻塞），而非仅单元测试不可用（CMakeLists.txt:160-161,167-168；注释已写明需 gcc-multilib 但无检测与优雅降级；本机已装 multilib，实测 ctest 10/10 全绿，unit-fs/unit-net 经 file 确认 ELF 32-bit）— 属实；已修 39f160b（CMakeLists.txt:162-181 用 check_c_source_compiles 在 configure 期以 -m32 探测 32 位宿主 libc，不可用时优雅跳过 unit-fs/unit-net 而非中断默认构建；ci.yml:42 增装 gcc-multilib 保持 CI 单测覆盖）；本机 32 位链可用，ctest 10/10 全绿
- [x] 审核发现：本轮 diff 未见问题（02f16c3a..HEAD，仅 CMakeLists.txt/ci.yml/todo.md）— 复核 m32 探测降级与 CI multilib 安装均正确；重新 configure/build 后本机 ctest 10/10 全绿。
- [x] 审核发现：审核完成 a962093..c22d096（rev-list count=3 含 ea42a7a/f47e4a0/c22d096，三者均仅改 todo.md，无代码问题）；本轮发现 2 项均已修；ctest 10/10
- [x] 审核发现：审计记录范围未覆盖 ea42a7a——第 54 行称「审核完成 ea42a7a..f47e4a0」，但 git 范围 ea42a7a..f47e4a0 仅含 f47e4a0（ea42a7a 为其直接父提交，被排除），而上一轮审计记录（第 53 行，落于 a962093）已止于 a962093，故 ea42a7a（docs: refresh blocked vitepress check）未被任何审计记录覆盖，审计追踪有缺口（todo.md:54）— 属实；已修：审计记录改为 a962093..c22d096（rev-list count=3 含 ea42a7a/f47e4a0/c22d096）；ctest 10/10
- [x] 审核发现：第 54 行格式不符本节约定——其余条目均为「- [x] 审核发现：…」复选框格式，该行以裸文本追加、无复选框且无「审核发现：」前缀，无法作为勾选项追踪（todo.md:54）— 属实；已修：第 54 行改为「- [x] 审核发现：…」复选框格式；ctest 10/10
- [x] 审核发现：本轮 diff 未见问题（c22d096..HEAD）— 不属实：c22d096..HEAD 并非未见问题，ded53d8 新增 `.vp-doc table { display:block; overflow-x:auto }` 冗余规则（rev-list count=3：9e8b568/ded53d8/d810f00），由下条核销；ctest 10/10
- [x] 审核发现：custom.css 新增 `.vp-doc table { display:block; overflow-x:auto }` 与 VitePress 默认规则（vp-doc.css:166-172 已含 display:block 与 overflow-x:auto）完全重复，为纯 no-op 冗余样式，可删除（docs/.vitepress/theme/custom.css:133-136）— 属实；已修（删除 docs/.vitepress/theme/custom.css:133-136 冗余规则）；docs build 绿、ctest 10/10
- [x] 审核发现：todo.md:57 新增裸文本行「不属实：…」无复选框且无「审核发现：」前缀，违反本节「- [x] 审核发现：…」约定、无法作为勾选项追踪，与第 56 行已修的同类格式问题一致（todo.md:57）— 属实；已修：todo.md:57 改为规范格式（勾选项，含「不属实：」注记）；修复已随 a98391f 提交；ctest 10/10
- [x] 审核发现：todo.md:59 新增条目自述「无代码变更故未 commit」，但本修复（todo.md:57 改规范格式）已随 a98391f 提交且工作树干净，「未 commit」与 git 事实矛盾（实为「未另立独立代码 commit」之意），且与同节第 54-58 行同类条目「已修…；ctest 10/10」的记录口径不一，易误导后续审计者误判该修复未入库（todo.md:59）— 属实；已修：todo.md:59 更正为「修复已随 a98391f 提交」（`git show a98391f -- todo.md` 含该修复）；ctest 10/10
- [x] 审核发现：本轮 diff 未见问题（a98391fc2f3ad881c9afa0160504b71b54d0c230..HEAD）— 属实：git diff a98391fc2f3ad881c9afa0160504b71b54d0c230..HEAD --stat 复核仅 todo.md 2+/1-（当日 HEAD=3a4fc936945264e807ad739435e0f015e03559f2），单 docs 提交且台账事实核对无问题；ctest 10/10（todo.md:61）
- [x] 审核发现：本轮 diff 未见问题（3a4fc936..HEAD）— 属实：`git diff 3a4fc936..HEAD --stat` 复核仅 `todo.md | 1 +`，范围仅 `5950ca3 docs: verify audit ledger round a98391f..HEAD`，无代码变更；`ctest --output-on-failure` 10/10 通过（todo.md:62）。
- [x] 审核发现：本轮 diff 未见问题（5950ca32..HEAD）— 属实：`git rev-list 5950ca32..HEAD` 仅含 `1f2c1b9`（docs: verify audit ledger round 3a4fc936..HEAD），`git diff 5950ca32..HEAD --stat` 复核仅 `todo.md | 1 +`，无代码变更；`ctest --output-on-failure` 10/10 通过。
