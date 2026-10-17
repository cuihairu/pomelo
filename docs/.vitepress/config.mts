import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Pomelo',
  description: 'A small i386 microkernel in C for self-study of kernel internals',
  base: '/pomelo/',
  head: [['link', { rel: 'icon', href: '/pomelo/logo.svg' }]],
  locales: {
    root: {
      label: 'English',
      lang: 'en',
      themeConfig: {
        nav: [
          { text: 'Guide', link: '/guide/intro' },
          { text: 'GitHub', link: 'https://github.com/cuihairu/pomelo' }
        ],
        sidebar: [
          {
            text: 'Getting started',
            items: [
              { text: '01 · What kind of kernel is this', link: '/guide/intro' },
              { text: '02 · Startup', link: '/guide/boot' },
              { text: '03 · Interrupt dispatch', link: '/guide/interrupts' },
              { text: '04 · Scheduling', link: '/guide/scheduling' },
              { text: '05 · IPC', link: '/guide/ipc' },
              { text: '06 · The syscall gate', link: '/guide/syscall' }
            ]
          },
          {
            text: 'User-space services',
            items: [
              { text: '07 · The disk format', link: '/guide/disk-format' },
              { text: '08 · The file service', link: '/guide/fs-server' },
              { text: '09 · The terminal service', link: '/guide/tty-server' },
              { text: '10 · shell', link: '/guide/shell' }
            ]
          },
          {
            text: 'Wrapping up',
            items: [
              { text: '11 · Building and running', link: '/guide/build-and-run' },
              { text: '12 · The PS/2 mouse', link: '/guide/mouse' },
              { text: '13 · Paging comes online', link: '/guide/paging' },
              { text: '14 · Real isolation: ring 3', link: '/guide/ring3' },
              { text: '15 · Fall down and get back up', link: '/guide/restart' }
            ]
          },
          {
            text: 'Networking',
            items: [
              { text: '16 · The network stack and socket service', link: '/guide/net' }
            ]
          }
        ]
      }
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      link: '/zh/',
      themeConfig: {
        nav: [
          { text: '教材', link: '/zh/guide/intro' },
          { text: 'GitHub', link: 'https://github.com/cuihairu/pomelo' }
        ],
        sidebar: [
          {
            text: '从零开始',
            items: [
              { text: '01 · 这是一个怎样的内核', link: '/zh/guide/intro' },
              { text: '02 · 启动', link: '/zh/guide/boot' },
              { text: '03 · 中断分发', link: '/zh/guide/interrupts' },
              { text: '04 · 调度', link: '/zh/guide/scheduling' },
              { text: '05 · IPC', link: '/zh/guide/ipc' },
              { text: '06 · 系统调用门', link: '/zh/guide/syscall' }
            ]
          },
          {
            text: '用户态服务',
            items: [
              { text: '07 · 磁盘格式', link: '/zh/guide/disk-format' },
              { text: '08 · 文件服务', link: '/zh/guide/fs-server' },
              { text: '09 · 终端服务', link: '/zh/guide/tty-server' },
              { text: '10 · shell', link: '/zh/guide/shell' }
            ]
          },
          {
            text: '收尾',
            items: [
              { text: '11 · 构建与运行', link: '/zh/guide/build-and-run' },
              { text: '12 · PS/2 鼠标', link: '/zh/guide/mouse' },
              { text: '13 · 分页上线', link: '/zh/guide/paging' },
              { text: '14 · 真隔离:ring3', link: '/zh/guide/ring3' },
              { text: '15 · 倒下与爬起', link: '/zh/guide/restart' }
            ]
          },
          {
            text: '网络',
            items: [
              { text: '16 · 网络栈与 socket 服务', link: '/zh/guide/net' }
            ]
          }
        ]
      }
    }
  },
  themeConfig: {
    logo: '/logo.svg',
    siteTitle: 'Pomelo',
    outline: [2, 3],
    socialLinks: [
      { icon: 'github', link: 'https://github.com/cuihairu/pomelo' }
    ]
  }
})
