# todo

- deps (rechecked 2026-10-10): vitepress latest is still 1.6.4 (2.x = 2.0.0-alpha.20); the 4 npm advisories in docs (vite ×3 — one high, one moderate + esbuild moderate) stay blocked on the 2.x stable, patched in vite ≥6.4.3 / esbuild ≥0.25.0.
- mmu (decided 2026-10-10, user approved): implement the beyond-mmu sketch steps 2-3 this batch — frame allocator, kernel page tables with paging on, shell and a fault probe running at ring3. Step 4 (ipc kernel copy + per-service page tables) remains the roadmap.
