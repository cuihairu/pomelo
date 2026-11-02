# todo

## 待办

- [ ] vitepress 2.x stable 后去 pnpm overrides 原生升级重验 build 与教材产物（触发：npm dist-tags latest 升 2.x，当前 latest=1.6.4、next=2.0.0-alpha.21；验收：tools/docs-chapters.sh save/check 34 页零漂移）
- [x] IPC 边界防御：fs/net 服务解 m->arg1/arg2 长度与 m->arg0 inode 索引前先校验负值/上限（发现 4-5，docs/review/2026-11-01-code-review.md）— servers/fs/fsrw.c 加 ino_at 校验索引/未用槽、do_write 负 len 归零、do_commit size≤MAXFILE；servers/net/net.c sock_consume/UDP/SEND 负 len 归零、夹上限；ctest 7/7
- [x] net 校验 ARP 帧长 ≥42（发现 6）— servers/net/net.c net_rx 加 len<ETH_HDR+28 拦截；ctest 7/7
- [x] fs do_commit 校验 size ≤ MAXFILE（发现 8）— b618ce7 IPC 提交 do_commit 已加 size≤MAXFILE 拒绝；ctest 7/7
- [ ] 清理小项：tty_getline 负参、ICMP 奇长校验和残留、walkthrough grep 过宽、mkfs next_block 守卫（发现 9-12）

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

- [x] 审核发现：cmd_write 在 fs_write 失败 break 后仍执行 fs_commit(ino,total) 并以 total 打印成功，部分写入被伪报为完整文件（apps/shell/cmd_fs.c:69-72）— 已修 apps/shell/cmd_fs.c，fs_commit 发布实际写入字节 off，打印 off；ctest 7/7
- [x] 审核发现：code-review 报告「约 341 个满文件即满」算术错误——1024 数据块 / 每满文件 6 块 ≈171，且 NINODES=32 上限（docs/review/2026-11-01-code-review.md:28）— 属实，见下修改 docs/review/2026-11-01-code-review.md:28 改为「约 171 个，且 NINODES=32 在文件数上更先封顶」
- [x] 审核发现：finding 7 状态不一致——报告与台账将 con_write 负长列为待办，但本次 4391cb5 的 SYS_WRITE user_ok 以无符号 ecx 已拒绝负长，报告自述修法已落地（docs/review/2026-11-01-code-review.md:60-62；todo.md:7）— 属实，见下
