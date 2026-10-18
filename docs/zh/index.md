---
layout: home

hero:
  name: Pomelo
  text: 一个用来自学内核知识的 i386 简易微内核
  tagline: C 实现的 i386 小内核 · 边读代码边学操作系统 · 每个目录就是一个概念 · 自学用途,非生产可用
  actions:
    - theme: brand
      text: 开始阅读
      link: /zh/guide/intro
    - theme: alt
      text: 本地跑起来
      link: /zh/guide/build-and-run
    - theme: alt
      text: 下载 nightly
      link: https://github.com/cuihairu/pomelo/releases/tag/nightly

features:
  - title: 微内核,只有五件事
    details: 内核里只有中断分发、调度、IPC、系统调用门和分页;文件系统、终端、shell、网络全部是跑在消息传递上的独立任务,各自住在 ring 3 的地址空间里。
  - title: 目录即教材目录
    details: boot/ 讲启动,kernel/ 讲内核五件事,servers/ 与 apps/ 讲服务。章节顺序就是代码目录顺序,每段代码都对应一个操作系统概念。
  - title: 两条命令跑起来
    details: cmake -B build && cmake --build build,QEMU 直接引导,不需要装 GRUB,不需要做 ISO。
---

<script setup lang="ts">
// 与英文首页对称:双向记录手动语言选择、双向跟随 localStorage,否则中文用户点 English 会被弹回
// 优先级:localStorage 手动选择 > navigator.language > 保持当前 locale
import { onBeforeMount } from 'vue'
const KEY = 'pomelo-lang-auto-picked'
const BASE = '/pomelo/'
const ROOT_PATH = BASE
const ZH_PATH = BASE + 'zh/'
onBeforeMount(() => {{
  if (typeof window === 'undefined') return
  const p = window.location.pathname
  if (p !== ZH_PATH && p !== ZH_PATH.slice(0, -1)) return
  const picked = localStorage.getItem(KEY)
  if (picked) {{ if (picked !== p) window.location.replace(picked); return }}
  const langs = (navigator.languages?.length ? navigator.languages : [navigator.language]).map(l => l.toLowerCase())
  if (!langs.some(l => l.startsWith('zh'))) window.location.replace(ROOT_PATH)
}})
if (typeof window !== 'undefined') {{
  window.addEventListener('click', (e) => {{
    const a = (e.target as HTMLElement)?.closest?.('a') as HTMLAnchorElement | null
    const href = a?.getAttribute('href') || ''
    if (href.startsWith(ZH_PATH) || href.startsWith('zh/')) localStorage.setItem(KEY, ZH_PATH)
    else if (href === ROOT_PATH || href === ROOT_PATH.slice(0, -1)) localStorage.setItem(KEY, ROOT_PATH)
  }}, true)
}}
</script>

