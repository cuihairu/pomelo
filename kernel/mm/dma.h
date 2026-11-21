#ifndef POMELO_DMA_H
#define POMELO_DMA_H

#include "../types.h"
#include "../intr/intr.h"        /* NIRQ */

/* One driver's DMA window, remembered per interrupt line.
 *
 * A driver that hands the card machine addresses cannot be trusted to
 * quiet the card when it dies: net has no orderly exit, a task_fault
 * frees its frames while the e1000 still writes into them, and a mask
 * on the pic does nothing about a bus master. The kernel therefore pins
 * every frame a task converts through SYS_V2P -- it stays allocated even
 * though the task that owned it is gone -- and gives the frames back only
 * when that line is claimed again. A new bringup resets the card (and
 * with it the DMA) before it opens the line, so the frames the old
 * driver pointed at are quiet by then. If nobody ever re-claims the
 * line, the frames stay held; on this machine that is a few dozen frames
 * of dead weight, which beats silently corrupting whatever frame the
 * card next writes into. */

#define NPIN 16                 /* net uses about ten distinct frames */

void dma_pin(u32 line, u32 pa);           /* mark a frame as still DMA'd */
int  dma_pinned(u32 pa);                 /* does frame_free dare touch it */
void dma_release(u32 line);               /* the card was reset: free them */

#endif
