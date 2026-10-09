#include "../../kernel/string.h"
#include "../../kernel/types.h"
#include "../../kernel/syscall/syscall.h"
#include "../../servers/tty/tty.h"

/* The isolation probe. It does one friendly thing -- announces itself
 * through the tty service, same protocol as the shell -- and then one
 * hostile thing: reads kernel memory. At ring 3 that address is a
 * supervisor page, so the CPU hands out a page fault and the kernel
 * kills this task. fs, tty and shell keep running; that is the whole
 * point. */

void probe_main(void);
__attribute__((section(".hdr")))
const unsigned probe_entry = (unsigned)probe_main;

void probe_main(void) {
    static const char hello[] = "probe: alive at ring 3\n";
    struct msg m;

    memset(&m, 0, sizeof m);
    m.type = MSG_TTY_PUTS;
    m.arg0 = (int)sizeof hello - 1;
    memcpy(m.data, hello, sizeof hello - 1);
    sys_send(TID_TTY, &m);

    volatile u32 *kernel = (volatile u32 *)0x101000;   /* kernel .bss */
    u32 s = *kernel;                                   /* #PF right here */
    (void)s;
}
