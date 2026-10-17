---
layout: home

hero:
  name: Pomelo
  text: A small i386 microkernel in C for self-study of kernel internals
  tagline: A small i386 kernel in C · Read the code and learn the OS at the same time · Each directory is one concept · For self-study, not production use
  actions:
    - theme: brand
      text: Start reading
      link: /guide/intro
    - theme: alt
      text: Run it locally
      link: /guide/build-and-run
    - theme: alt
      text: Download nightly
      link: https://github.com/cuihairu/pomelo/releases/tag/nightly

features:
  - title: A microkernel with only five jobs
    details: The kernel only has interrupt dispatch, scheduling, IPC, the syscall gate and paging. The filesystem, terminal, shell and network are all standalone tasks that run on top of message passing, each in its own ring 3 address space.
  - title: The directory tree is the curriculum
    details: boot/ covers startup, kernel/ covers the five jobs of the kernel, servers/ and apps/ cover the services. The chapter order is the directory order, and each piece of code maps to one operating system concept.
  - title: Up and running in two commands
    details: cmake -B build && cmake --build build. QEMU boots directly — no GRUB to install, no ISO to make.
---

<script setup lang="ts">
// 首次访问按浏览器语言选择文档语言（vitepress 无此能力，GitHub Pages 不支持 _redirects）
// 优先级：localStorage 手动选择 > navigator.language > 保持当前 locale
import { onBeforeMount } from 'vue'
const KEY = 'pomelo-lang-auto-picked'
const BASE = '/pomelo/'
const ROOT_PATH = BASE
const ZH_PATH = BASE + 'zh/'
onBeforeMount(() => {{
  if (typeof window === 'undefined') return
  const p = window.location.pathname
  if (p !== ROOT_PATH && p !== ROOT_PATH.slice(0, -1)) return
  const picked = localStorage.getItem(KEY)
  if (picked) {{ if (picked !== p) window.location.replace(picked); return }}
  const langs = (navigator.languages?.length ? navigator.languages : [navigator.language]).map(l => l.toLowerCase())
  if (langs.some(l => l.startsWith('zh'))) window.location.replace(ZH_PATH)
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


