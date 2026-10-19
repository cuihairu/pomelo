# todo

## 待办

- [x] docs/guide/ 16 章双语配对(16 个 .en.md 落地、跨章链接全部 .en 化、32 处切换器字节级一致、结构 1:1、vitepress build 绿;根目录 index 对见 c4ea186)
- [ ] vitepress 2.x 出 stable 后去掉 pnpm overrides 原生升级,重验 build 与教材产物
  - 状态:阻塞。原因:上游 vitepress 2.x 仍只有 alpha(latest=1.6.4、next=2.0.0-alpha.20,alpha.20 发布于 2026-09-04,5 周无新发布);依赖链内无范围内已修补版本(vite 已修补 6.4.3+ 在 vitepress 1.6.4 的 ^5.4.14 之外,esbuild 0.25.x 同理)。触发条件:npm dist-tags 的 latest 升到 2.x stable。所需输入:上游发布;届时动作=升 ^2.x、去 overrides、pnpm install、build 绿、16 章产物无回归、单 commit。

## 已完成

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
- nightly 观察:2026-10-18 的 cron(21:37 UTC)截至 23:34 UTC 未出 run(已 1h57m),仍在 2026-10-14 先例的约 3 小时 GitHub 延迟窗口内;交付面无缺口(release publishedAt 2026-10-17T23:24:37Z、7 资产齐;10-18 仅 docs 提交,产物与 10-17 构建无差异,48 小时更新口径不破);处置=只观察,不 dispatch(无修复待验证,重复构建同一 commit 无价值);若 10-19 cron(21:37 UTC)再缺才升级处理。
- L5 维持阻塞:R2 复测 dist-tags 仍 latest=1.6.4、next=2.0.0-alpha.20,与 FIRE1019 复核一致。
- 基线:vitepress build 绿(33.0s,含新增 zh 首页 script);docs-only 改动按口径不跑 ctest 与主构建。

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
