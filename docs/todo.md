# todo

## 待办

- [x] docs/guide/ 16 章双语配对(16 个 .en.md 落地、跨章链接全部 .en 化、32 处切换器字节级一致、结构 1:1、vitepress build 绿;根目录 index 对见 c4ea186)
- [ ] vitepress 2.x 出 stable 后去掉 pnpm overrides 原生升级,重验 build 与教材产物
  - 状态:阻塞。原因:上游 vitepress 2.x 仍只有 alpha(latest=1.6.4、next=2.0.0-alpha.21,alpha.21 发布于 2026-10-25,距 alpha.20 约 7 周);依赖链内无范围内已修补版本(vite 已修补 6.4.3+ 在 vitepress 1.6.4 的 ^5.4.14 之外,esbuild 0.25.x 同理)。触发条件:npm dist-tags 的 latest 升到 2.x stable。所需输入:上游发布;届时动作=升 ^2.x、去 overrides、pnpm install、build 绿、16 章产物无回归、单 commit。(2026-10-28 复核:`npm view vitepress dist-tags`=latest=1.6.4、next=2.0.0-alpha.21,全版本序列止于 alpha.21(2026-10-25T13:00:10Z 发布,距 alpha.20 约 7 周),2.x 仍无 stable,保持未勾)
  - 仓内预备:tools/docs-chapters.sh save/check 承接「16 章产物无回归」验收——升级前 save 建基线、升级后 check 逐一比对 34 个站点页(17 页×中英,归一化 asset hash 后只认正文漂移),四路径已验(save 34 页、零漂移 exit 0、注入漂移 exit 1、无基线 exit 2)。

## 已完成

- [x] README 两语种修「默认机型是 q35」误写:本地 `qemu-system-i386 -machine help` 默认 pc-i440fx 系、教材 docs/guide/build-and-run.md:31 亦写「default is `pc`」,改为「默认即 pc,特意写明;-M q35 才把盘挪到 AHCI 后」(42f52c2,登审计表 A13,docs build 绿)
- [x] zh 首页补镜像语言记忆脚本,修复中文用户点 English 被自动检测弹回的死循环(19f6230,vitepress build 绿,产物逐条核对:picked 双向优先、非 zh 浏览器入 zh 首页回英文根、listener 双向记录)
- [x] vitepress 官方 locales 双语落地(英文 17 页在 docs/ 根、中文 17 页在 docs/zh/,官方语言切换器替代手写切换链接,构建绿;见根 todo.md 2026-10-17 用户令)
- [x] clear 与 reboot 各有专属冒烟测试,审计表口径改六测(0c135b0,ctest 6/6)
- [x] 纯文档 push 不再触发主 CI,README 本地测试口径落地(9e08ce3,ctest 4/4)
- [x] docs 依赖告警清零,pnpm overrides 钉 vite 6.4.3 与 esbuild 0.25.0(a33d004、c2c0017,pnpm audit 零告警)
- [x] nightly 双机型覆盖,判决不过不刷 release(0ff6dfc、7d959ac,ctest 4/4)
- [x] smoke 断言聚合与 walkthrough 判分补强(7e622c2、5a0c637,ctest 4/4)
- [x] help 输出与命令表对表断言(aacd8b5,ctest 4/4)
- [x] 教材 16 章逐章源码对齐(dc5b20d..911d324,vitepress build 绿)
- [x] 文档门面与 README 审计闭环,A1-A6(见 docs/审计-文档一致性.md)

## 台账依据(2026-10-13 R1013B)

- 枚举:find 穷尽全仓 *todo*/*roadmap*(排除 build/node_modules/.git/dist),仅 docs/todo.md 一个清单文件;TODO.md、ROADMAP.md、docs/roadmap.md 均不存在。
- 无遗漏依据:本轮四查均无新项——失败测试(本地 ctest 4/4 绿,ci.yml 与 docs.yml 最近 run 绿)、CI 红(唯一失败为 08:14Z 旧 docs run,已被 c2c0017 修复并两次绿 run 盖过)、文档过期(A7-A11 已闭环,见 docs/审计-文档一致性.md)、依赖告警(pnpm audit 零告警)。

## 台账依据(2026-10-14 复核)

- 上游:vitepress `latest=1.6.4`、`next=2.0.0-alpha.20`——2.x 仍无 stable;vite 5.4.x 止于 5.4.21、esbuild 0.24.x 止于 0.24.2,overrides 仍承重,未完成项维持阻塞。
- 已完成条目引用 hash:11 个全部存在且标题相符(`git rev-parse --verify` 逐个核过)。
- nightly:修复后首个 schedule run 被 GitHub 延迟约 3 小时(run 38490891018,00:33 UTC 才触发),三任务绿、release 刷新(publishedAt 2026-10-14T01:05:54Z)、7 资产齐;延迟期间以 dispatch 补齐(22:08 UTC 那次亦三任务绿)。
- 测试/CI/依赖:本地 ctest 4/4 绿(40.9s);ci.yml 与 docs.yml 在 `8422a1f` 均 success;`pnpm audit` 零告警。

## 台账依据(2026-10-15 复核)

- 上游:vitepress `latest=1.6.4`、`next=2.0.0-alpha.20`(alpha.20 后 6 周无新发布)——2.x 仍无 stable,唯一未完成项维持阻塞,触发条件未到,无新动作可做。
- 测试/CI/依赖:本地 ctest 6/6 绿(61.1s,含 clear/reboot 新冒烟);ci.yml、docs.yml 最近 run 在 `4a637fe` 均 success,nightly schedule run(2026-10-14 00:33 UTC)三任务绿;`pnpm audit` 零告警。
- 审计表自漂移:「已核一致」抽样仍记 A10 修复前的 `npm --prefix docs run dev`,与表内 A10 处置结果矛盾;已按两 README 现状(README.md:103、README.zh.md:80)改正,禁词口径复点仍零命中。
- README 单侧漂移:cdab589 只给 README.md 加语言切换行,README.zh.md 缺镜像;已补并登审计表 A12。远端门禁实证:cbdf8c9、0528d52 两笔纯文档 push 各只触发 docs.yml(均 success),ci.yml 按 paths-ignore 跳过。

## 台账依据(2026-10-19 R2 巡检)

- 新修:zh 首页缺镜像语言记忆脚本——4106ac3 的 click listener 只在英文页注册,浏览器语言为 zh 的用户从中文页点 English 到达英文根页后,会被 navigator.language 自动检测立即 replace 回中文页,UI 层无法停留英文首页;修复=docs/zh/index.md 加对称脚本(19f6230),产物核对见已完成区。
- nightly 观察(已解):2026-10-18 的 cron(21:37 UTC)一度到 23:34 UTC 未出 run(1h57m),判定仍在 2026-10-14 先例的约 3 小时 GitHub 延迟窗口内,处置=只观察不 dispatch(无修复待验证,重复构建同一 commit 无价值);实际 run 于 2026-10-18T23:35:29Z 触发、success、2m31s(延迟 1h58m,仍在先例窗口内),release 刷新于 23:37:58Z、7 资产齐,交付面无缺口(10-18 仅 docs 提交,产物与 10-17 构建无差异,48 小时更新口径不破)。10-19 的 cron(21:37 UTC)照常,无需升级处理。
- 残留清理:两内部台账页头部的 `> **中文** — …暂无译版。` 为 c4ea186 手写切换器方案残留——6bfce8e 只删 docs/index.md 一行,50c2ec7(locales 落地)只给 docs/todo.md 加 1 行 [x]、从未触碰 docs/审计-文档一致性.md,故两行漏网,且与根 todo.md 自记「手写切换器全删」矛盾;已在 docs 两内部页删除(27163b8),README.md:3 / README.zh.md:3 的门面成对切换行(A12 登记特性、不在站点内)保留。
- 审计表行号时效:表内 A6 记 docs/todo.md:3、A9 记 docs/todo.md:5,系 2026-10-13 快照行号;10-16 加头部行与 [x] 行后已漂移,本轮删头部行后开放项在 :6。发现行的历史证据不做回改,此处留记以免误读。
- L5 维持阻塞:R2 复测 dist-tags 仍 latest=1.6.4、next=2.0.0-alpha.20,与 FIRE1019 复核一致。
- 基线:vitepress build 绿(33.0s,含新增 zh 首页 script);docs-only 改动按口径不跑 ctest 与主构建。
- 抽样复点:kernel+boot 仍 33 文件 2034 行(find | wc -l / cat | wc -l),与 docs/guide/intro.md:27-28 及审计表抽样项一致,无漂移。
- 远端:本轮三笔 docs push(19f6230、e00f848、27163b8)各只触发 docs.yml 且均 success,主 CI 按 paths-ignore 跳过。

## 台账依据(2026-10-19 FIRE1019R2 复核)

- L5 仍阻塞:`npm view vitepress dist-tags` → `latest=1.6.4`、`next=2.0.0-alpha.20`,与上两轮一致;已尝试=三次独立复测 dist-tags、查全版本序列(止于 alpha.20,无 beta/rc/stable);解除条件未到,无本地可行动作。
- 无漏登事项:全仓 grep TODO/FIXME/XXX/HACK(kernel、boot、apps、servers、docs、workflow、sh)零命中;仓库门面元数据(A5)无漂移(description 含 fs/tty/shell/net 与 VitePress、8 个 topics 齐)。
- 审计表抽样全表复核(9 项):CMakeLists.txt:115-120、122-149、tools/nightly-run.sh:1-16/40-42/69、ci.yml paths-ignore、nightly.yml matrix[q35,pc]+cron 37 21 * * *、boot/main.c:21-42(`servers_start()` 六个 prog_add)均与现状一致;禁词零命中(唯一命中是审计表自述的 grep 模式,非正文使用)。
- 三处抽样行号已改正(intro.md:13-28 → :11-20,原范围含 22-28 无关的单体/微内核比较表;intro.md:27-28 → :20,33/2034 主张所在行;README.zh.md:80 → :82,A12 补镜像行后下移),33 文件 2034 行含分项(kernel/ 28 文件 1835 行 + boot/ 5 文件 199 行)精确成立,zh 镜像 intro.md:26 同数。
- 结论:除 L5 外部依赖外无可推进项,无代决事项。

## 台账依据(2026-10-19 FIRE1019 复核)

- 唯一未完成项(L5,vitepress 2.x stable 触发)复核:`npm view vitepress dist-tags` → `latest=1.6.4`、`next=2.0.0-alpha.20`;全版本序列止于 alpha.20(发布于 2026-09-04,至今 6 周半无任何新版),2.x 仍无 stable;解锁条件未成立,维持阻塞,不重开,不越界。
- 解锁后动作维持预置:docs/package.json 升 `vitepress: ^2.x`、去 `docs/pnpm-workspace.yaml` overrides(vite 6.4.3、esbuild 0.25.0)、pnpm install、build 绿、16 章产物无回归、单 commit。
- 基线:本地 ctest 6/6 绿(60.4s);docs build 绿(6.2s);`pnpm --prefix docs audit` 零告警;远端 docs.yml(b50596a)与 nightly schedule(2026-10-17)均 success。
- 双语结构复点:docs/guide 16 章 ↔ docs/zh/guide 16 章文件名 1:1(diff 为空),审计表 A1-A12 全闭环,无新增漂移。
- 无代决事项:唯一阻塞项属外部依赖,条件未达前无可拍板动作。

## 台账依据(2026-10-16 PATROL1016 复核)

- 唯一未完成项(L5,vitepress 2.x stable 触发)复核:`npm view vitepress dist-tags` → `latest=1.6.4`、`next=2.0.0-alpha.20`,解锁条件仍未成立;维持阻塞,不重开,不越界。
- 解锁后动作维持预置:docs/package.json 升 `vitepress: ^2.x`、去 `docs/pnpm-workspace.yaml` overrides(vite 6.4.3、esbuild 0.25.0)、pnpm install、build 绿、16 章产物无回归、单 commit。
- 基线:本地 ctest 6/6 绿(60.9s);nightly 于 `97132e7` success、docs.yml 于 `97132e7` success;`pnpm audit` 零告警。
- 无代决事项:唯一阻塞项属外部依赖,条件未达前无可拍板动作。

## 审核发现

- [x] 审核发现：状态句仍称 alpha.20 后「5 周无新发布」，与同一行新增的 alpha.21（2026-10-25）及「距 alpha.20 约 7 周」事实自相矛盾，应同步更新原因句。（docs/todo.md:7）
- [x] 审核发现：send() 的提示符门控在 grep 出错时失败开放——若 $out 尚未创建（后台 qemu 子进程先 `mkfifo` 读端、写端汇合后才 `> "$out"` 建文件，存在启动竞态），`grep -c` 无输出使 `[ "" -lt "$want" ]` 报 "Illegal number" 判假、跳过等待，首个命令在 shell 提示符出现前即被写入，且已无原 pacing 的首字节吞没补偿换行，等于重引入本次修复要消除的早发丢失。（tools/walkthrough.sh:43-46）已修：`: > "$out"` 先建转录、`count=$(grep -c ...) || count=0` 兜底，缺转录读为 0 不再失败开放；证据：复现 `[: Illegal number` 且循环不进入，修复后 walkthrough 绿（walkthrough ok: /tmp/walk-test.log）。
- [x] 审核发现：send() 的等待预算按单条命令计（88×0.25=22s），11 条命令共用无整体看门狗——启动后从未出现 `pomelo> `（或 qemu 提前退出）时驱动可阻塞约 253s，约为注释 "give up only near the timeout" 所指 25s 上限的 10 倍。（tools/walkthrough.sh:44）已修：改为整组单一 `deadline=now+25s` 看门狗，超时即跳过该命令；证据：坏 kernel 启动（无提示符）实测 24.6s 退出（time 输出 real 0m24.583s），不再 ~253s。
- [x] 审核发现：`exec 3>"$fifo"`（walkthrough.sh:37）在 qemu 未存活/未能打开读端时永久阻塞——`open()` 写端要等读端先打开，若 qemu 因错误路径提前退出（如 bad kernel/args、无 timeout 保护外的崩溃），驱动在此挂死，`timeout 25` 只作用于 qemu 子进程、不覆盖父 shell；该行处于任何命令发送与 `wait` 之前，脚本将卡死而非超时。（tools/walkthrough.sh:37）已修：改 `exec 3<>"$fifo"`（O_RDWR，写端打开不等待读端）；证据：无读端时 `exec 3>fifo` 超时 exit=124，`exec 3<>fifo` 即时返回 exit=0，修复后坏 kernel 场景 24.6s 退出不挂死。
- [x] 审核发现：prompt-gated walkthrough 修复没有 push/PR 自动验证，ci.yml 仅运行 ctest 而 walkthrough 未注册为 CTest，回归可等到 nightly 才暴露（.github/workflows/ci.yml:51）。已修：CMakeLists.txt 注册 `walkthrough` 测试（`add_test` + TIMEOUT 120），ci.yml 的 `ctest` 现为 7 测含 walkthrough；证据：`ctest --test-dir build --output-on-failure` 7/7 绿（87.6s，walkthrough 25.31s，输出 `walkthrough ok: walkthrough.log`）。
