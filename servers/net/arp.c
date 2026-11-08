#include "../../kernel/string.h"
#include "proto.h"

/* Address resolution. One cache entry -- the gateway's mac -- because
 * under SLIRP every peer appears on-link through the gateway. Requests
 * for our own address get an answer: that is what makes us pingable
 * later. */

u8  arp_mac[6];
int arp_ready;

/* Who has this ip? Broadcast the question; the answer lands in pump.
 * 42 bytes: arp needs no minimum-size pad for this peer, and shorter
 * frames leave more of the one tx buffer alone. */
void arp_request(const u8 ip[4]) {
    u8 f[42];                             /* 14 header + 28 arp */
    memcpy(f, ETH_BROADCAST, 6);
    memcpy(f + 6, mac, 6);
    f[12] = ETH_ARP >> 8;
    f[13] = ETH_ARP & 0xFF;
    f[14] = 0; f[15] = 1;                 /* hardware type: ethernet */
    f[16] = 0x08; f[17] = 0;              /* protocol type: ipv4 */
    f[18] = 6; f[19] = 4;                 /* hlen, plen */
    f[20] = 0; f[21] = 1;                 /* operation: request */
    memcpy(f + 22, mac, 6);               /* sender hardware address */
    memcpy(f + 28, ip_self, 4);           /* sender protocol address */
    memset(f + 32, 0, 6);                 /* target hardware: unknown */
    memcpy(f + 38, ip, 4);                /* target protocol: the question */
    nic_tx(f, sizeof f);
}

/* An arp frame came in. Replies fill the cache; requests for our own
 * address get an answer -- that is what makes us pingable later. */
void arp_in(const u8 *a) {
    u16 op = (u16)((a[6] << 8) | a[7]);
    if (op == 2 && !memcmp(a + 14, ip_gw, 4)) {   /* reply from the gate */
        memcpy(arp_mac, a + 8, 6);
        arp_ready = 1;
        return;
    }
    if (op == 1 && !memcmp(a + 24, ip_self, 4)) { /* who has us? */
        u8 f[42];
        memcpy(f, a + 8, 6);              /* reply to the asker's mac */
        memcpy(f + 6, mac, 6);
        f[12] = ETH_ARP >> 8;
        f[13] = ETH_ARP & 0xFF;
        f[14] = 0; f[15] = 1;
        f[16] = 0x08; f[17] = 0;
        f[18] = 6; f[19] = 4;
        f[20] = 0; f[21] = 2;             /* operation: reply */
        memcpy(f + 22, mac, 6);
        memcpy(f + 28, ip_self, 4);
        memcpy(f + 32, a + 8, 6);         /* the asker becomes the target */
        memcpy(f + 38, a + 14, 4);
        nic_tx(f, sizeof f);
    }
}
