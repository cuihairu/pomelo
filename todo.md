# todo

## 待办

- [ ] vitepress 2.x 出 stable 后去掉 pnpm overrides 原生升级,重验 build 与教材产物
  - 阻塞:上游 2.x 仍只有 alpha(npm dist-tags latest=1.6.4、next=2.0.0-alpha.21,2026-11-01 复核);依赖链内无范围内已修补版本(vite 已修补 6.4.4 在 vitepress 1.6.4 的 ^5.4.14 之外,esbuild 0.25.x 同理)
  - 触发条件:npm dist-tags 的 latest 升到 2.x stable;届时动作=docs/package.json 升 ^2.x、去 docs/pnpm-workspace.yaml overrides、pnpm install、build 绿、16 章产物无回归、单 commit
  - 验收门:tools/docs-chapters.sh save/check——升级前 save 建基线、升级后 check 比对 34 个站点页(17 页×中英,归一化 asset hash 只认正文漂移);四路径已验(save 34 页、零漂移 exit 0、注入漂移 exit 1、无基线 exit 2)

## 已完成

- [x] vitepress 官方 locales 双语落地(英文 17 页 docs/ 根、中文 17 页 docs/zh/) — 50c2ec7, build 绿
- [x] docs/guide 16 章双语配对 — c4ea186, build 绿
- [x] 教材 16 章逐章源码对齐 — dc5b20d..911d324, build 绿
- [x] README 修「默认机型 q35」误写 — 42f52c2, docs build 绿
- [x] zh 首页镜像语言记忆脚本 — 19f6230, build 绿
- [x] clear/reboot 专属冒烟测试 — 0c135b0, ctest 6/6
- [x] 纯文档 push 跳过主 CI — 9e08ce3, ctest 4/4
- [x] docs 依赖告警清零(overrides 钉版) — a33d004/c2c0017, pnpm audit 零告警
- [x] nightly 双机型覆盖,不过不刷 release — 0ff6dfc/7d959ac, ctest 4/4
- [x] smoke 断言聚合与 walkthrough 判分补强 — 7e622c2/5a0c637, ctest 4/4
- [x] help 输出与命令表对表断言 — aacd8b5, ctest 4/4
- [x] walkthrough 提示符门控加固并注册为 ctest — df48ad7/91d5cab/1f1c811, ctest 7/7
- [x] 文档门面与 README 审计 A1-A6 — 见 docs/审计-文档一致性.md
- [x] 台账合并入根 todo.md 并恢复丢失记录 — a02f5ea/707be8a, docs-only
- [x] README 徽章行修复:吞标题、URL 双斜杠、单行居中、A13 回归、英文段中文 — 4eedc33, markdown-it 复核 + docs build 绿
