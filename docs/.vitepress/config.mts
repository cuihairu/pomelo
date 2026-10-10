import { defineConfig } from 'vitepress'

export default defineConfig({
  lang: 'zh-CN',
  title: 'Pomelo',
  description: '一个写给大家看的微内核:边读代码边学操作系统',
  base: '/pomelo/',
  head: [['link', { rel: 'icon', href: '/pomelo/logo.svg' }]],
  themeConfig: {
    logo: '/logo.svg',
    siteTitle: 'Pomelo',
    nav: [
      { text: '教材', link: '/guide/intro' },
      { text: 'GitHub', link: 'https://github.com/cuihairu/pomelo' }
    ],
    sidebar: [
      {
        text: '从零开始',
        items: [
          { text: '01 · 这是一个怎样的内核', link: '/guide/intro' },
          { text: '02 · 启动', link: '/guide/boot' },
          { text: '03 · 中断分发', link: '/guide/interrupts' },
          { text: '04 · 调度', link: '/guide/scheduling' },
          { text: '05 · IPC', link: '/guide/ipc' },
          { text: '06 · 系统调用门', link: '/guide/syscall' }
        ]
      },
      {
        text: '用户态服务',
        items: [
          { text: '07 · 磁盘格式', link: '/guide/disk-format' },
          { text: '08 · 文件服务', link: '/guide/fs-server' },
          { text: '09 · 终端服务', link: '/guide/tty-server' },
          { text: '10 · shell', link: '/guide/shell' }
        ]
      },
      {
        text: '收尾',
        items: [
          { text: '11 · 构建与运行', link: '/guide/build-and-run' },
          { text: '12 · PS/2 鼠标', link: '/guide/mouse' },
          { text: '13 · 分页上线', link: '/guide/paging' },
          { text: '14 · 真隔离:ring3', link: '/guide/ring3' },
          { text: '15 · 倒下与爬起', link: '/guide/restart' }
        ]
      }
    ],
    outline: [2, 3],
    socialLinks: [
      { icon: 'github', link: 'https://github.com/cuihairu/pomelo' }
    ]
  }
})
