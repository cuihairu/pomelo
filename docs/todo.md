# todo

## 待办

- [ ] vitepress 2.x 出 stable 后去掉 pnpm overrides 原生升级,重验 build 与教材产物
  - 状态:阻塞。原因:上游 vitepress 2.x 仍只有 alpha(latest=1.6.4、next=2.0.0-alpha.20,alpha.20 发布于 2026-09-04,5 周无新发布);依赖链内无范围内已修补版本(vite 已修补 6.4.3+ 在 vitepress 1.6.4 的 ^5.4.14 之外,esbuild 0.25.x 同理)。触发条件:npm dist-tags 的 latest 升到 2.x stable。所需输入:上游发布;届时动作=升 ^2.x、去 overrides、pnpm install、build 绿、16 章产物无回归、单 commit。

## 已完成

- [x] docs 依赖告警清零,pnpm overrides 钉 vite 6.4.3 与 esbuild 0.25.0(a33d004、c2c0017,pnpm audit 零告警)
- [x] nightly 双机型覆盖,判决不过不刷 release(0ff6dfc、7d959ac,ctest 4/4)
- [x] smoke 断言聚合与 walkthrough 判分补强(7e622c2、5a0c637,ctest 4/4)
- [x] help 输出与命令表对表断言(aacd8b5,ctest 4/4)
- [x] 教材 16 章逐章源码对齐(dc5b20d..911d324,vitepress build 绿)
- [x] 文档门面与 README 审计闭环,A1-A6(见 docs/审计-文档一致性.md)

## 台账依据(2026-10-13 R1013B)

- 枚举:find 穷尽全仓 *todo*/*roadmap*(排除 build/node_modules/.git/dist),仅 docs/todo.md 一个清单文件;TODO.md、ROADMAP.md、docs/roadmap.md 均不存在。
- 无遗漏依据:本轮四查均无新项——失败测试(本地 ctest 4/4 绿,ci.yml 与 docs.yml 最近 run 绿)、CI 红(唯一失败为 08:14Z 旧 docs run,已被 c2c0017 修复并两次绿 run 盖过)、文档过期(A7-A10 上轮已修齐)、依赖告警(pnpm audit 零告警)。
