#ifndef POMELO_FRAME_H
#define POMELO_FRAME_H

#include "../types.h"

/* Physical 4K frames, the raw currency of paging. */

void frame_init(void);
u32  frame_alloc(void);     /* returns a physical address, 0 = out of memory */
void frame_free(u32 pa);

#endif
