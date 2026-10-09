#ifndef POMELO_GDT_H
#define POMELO_GDT_H

#include "../kernel/types.h"

/* Ring 3 selectors, GDT slots 3 and 4 with the RPL bits set. */
#define USER_CS 0x1B
#define USER_DS 0x23

void gdt_init(void);
void tss_esp0_set(u32 top);   /* kernel stack for the next ring3 gate */

#endif
