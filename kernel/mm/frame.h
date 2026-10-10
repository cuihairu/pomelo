#ifndef POMELO_FRAME_H
#define POMELO_FRAME_H

#include "../types.h"

/* Physical 4K frames, the raw currency of paging. */

struct mem_info {           /* what SYS_MEM hands a task: the bitmap, counted */
    u32 total;
    u32 used;
    u32 free;
};

void frame_init(void);
u32  frame_alloc(void);     /* returns a physical address, 0 = out of memory */
void frame_free(u32 pa);
void frame_stats(struct mem_info *out);   /* one consistent snapshot */

#endif
