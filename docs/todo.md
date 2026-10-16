> **中文** — 本页为仓库内部任务台账,暂无译版。

# todo

## 待办

- [x] docs/guide/ 16 章双语配对(16 个 .en.md 落地、跨章链接全部 .en 化、32 处切换器字节级一致、结构 1:1、vitepress build 绿;根目录 index 对见 c4ea186)
- [ ] vitepress 2.x 出 stable 后去掉 pnpm overrides 原生升级,重验 build 与教材产物
  - 状态:阻塞。原因:上游 vitepress 2.x 仍只有 alpha(latest=1.6.4、next=2.0.0-alpha.20,alpha.20 发布于 2026-09-04,5 周无新发布);依赖链内无范围内已修补版本(vite 已修补 6.4.3+ 在 vitepress 1.6.4 的 ^5.4.14 之外,esbuild 0.25.x 同理)。触发条件:npm dist-tags 的 latest 升到 2.x stable。所需输入:上游发布;届时动作=升 ^2.x、去 overrides、pnpm install、build 绿、16 章产物无回归、单 commit。

## 已完成

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

## 台账依据(2026-10-16 PATROL1016 复核)

- 唯一未完成项(L5,vitepress 2.x stable 触发)复核:`npm view vitepress dist-tags` → `latest=1.6.4`、`next=2.0.0-alpha.20`,解锁条件仍未成立;维持阻塞,不重开,不越界。
- 解锁后动作维持预置:docs/package.json 升 `vitepress: ^2.x`、去 `docs/pnpm-workspace.yaml` overrides(vite 6.4.3、esbuild 0.25.0)、pnpm install、build 绿、16 章产物无回归、单 commit。
- 基线:本地 ctest 6/6 绿(60.9s);nightly 于 `97132e7` success、docs.yml 于 `97132e7` success;`pnpm audit` 零告警。
- 无代决事项:唯一阻塞项属外部依赖,条件未达前无可拍板动作。
