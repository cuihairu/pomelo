# 16 · The network stack and socket service

Corresponding code: `servers/net/net.c`, `apps/netsh/netsh.c`

## The NIC driver lives in user mode

[Chapter 8](/guide/fs-server) set one rule: the ATA ports belong to the fs task, the kernel only delivers messages. The network card does the same — the e1000's PCI configuration space, the MMIO registers, the transmit and receive descriptor rings, the whole driver lives in the ring 3 net task. fs reaches the ports on the IOPL=3 it was born with; net reaches PCI on the same privilege (see `EFLAGS_USER_IOPL` in `boot/main.c`); the part the kernel shows up for is only the interrupt line: `SYS_IRQ_ENABLE` unmasks one pin on the PIC, and when the interrupt comes it uses `irq_wait` to wake the task — the same treatment as the keyboard in [Chapter 9](/guide/tty-server).

MMIO is the new face. Device registers are mapped on physical addresses; the task first gets a virtual address in exchange through `SYS_MMIO`, then reads and writes the registers just like memory; the descriptor ring is for DMA's use, so it takes `SYS_V2P` to translate the virtual address into the physical address the card recognizes. These two doors are pure paging mechanisms; the full account is recorded in [Chapter 13](/guide/paging). The kernel's knowledge of the network card ends here: it does not know what an e1000 is, only that some task is waiting on some interrupt line.

Both directions are descriptor rings: a string of fixed-length "envelopes", each wrapping one buffer address. After receiving a packet the card writes the status back into the envelope and pulls the interrupt line; the driver sweeps the ring, takes the packets away, and hangs the envelopes back up. There is no clever queue-overflow strategy — when the ring is full they are just dropped, and neither the card nor the driver pretends not to see it.

## Four layers, one function per layer

In net.c the protocol stack is nothing but a call chain: `net_rx` reads the Ethernet type number, ARP packets go to `arp_in`, IP packets to `ip_in`; `ip_in` reads the protocol number, ICMP echo requests are answered on the spot, UDP and TCP packets find a socket by destination port and are handed to `sock_in_udp` / `sock_in_tcp`. ARP asks the gateway's MAC first; ICMP trades one round trip with the gateway — each of these two things happens once at boot, there are words on the serial port, there are frames in pcap, trustworthy and checkable.

The checksum deserves a mention of its own. The checksums of both UDP and TCP cover a "pseudo-header" — source and destination IP, protocol number and length pieced together in front of the data and computed in one pass (rfc 768 / 793). The two protocols share one single `pseudo_cksum`; the only difference is the protocol number parameter. This parameter was hard-coded to UDP at the start, and the result was that every TCP segment got silently dropped by the peer — the host never answers, and pcap shows nothing but retransmitted SYNs. The lesson is recorded here: **two protocols looking alike does not mean one constant can serve both of them.**

## TCP: a small and honest state machine

The TCP in the stack has only four states: LISTEN, SYN_SENT, SYN_RCVD, ESTABLISHED. No option negotiation, no SACK, no congestion control, no out-of-order segments accepted, fixed initial sequence number, fixed window, one socket one connection at a time. These gaps are written in the code's comments, not hidden away in a design document: the peer is the test tools on the host machine, segments arrive in order, and when one arrives out of order it sends duplicate ACKs to beg for a retransmission — **the definition of "good enough" depends on who the peer is, and this stack knows who it serves.**

The dependable part is retransmission. A segment sent but not acknowledged is retransmitted once every 50 heartbeats (about half a second); eight rounds without an answer and the connection is written off, waking the caller parked on it. FIN travels the same road: if the data is not fully confirmed yet at close, it waits for the retransmission heartbeat to clear the line before sending FIN — **when tearing down a connection and sending data compete for the same line, it queues; it does not cut in line.**

```c
if (!s->unacked_len) {           /* only a parked close remains */
    tcp_send(s, TCP_ACK | TCP_FIN, 0, 0);
    sock_drop(s);
    continue;
}
```

## socket: turning the protocol stack into a service

Everything up to the previous section was "one task chatting happily with itself". The socket layer opens the stack outward: the net task holds a four-seat socket table, and the app speaks in messages —

| Message | Request | Reply |
| --- | --- | --- |
| OPEN | protocol, local port, peer (all zero = listening) | arg0=socket, arg1=local port |
| ACCEPT | socket | arg0=socket, data=peer ip+port |
| RECV | socket, limit | arg0=length, arg1=-1 means the peer ended |
| SEND | socket, length, data | arg0=length sent |
| CLOSE | socket | no reply |
| ERR | (not a request, a rejection letter) | arg1=rejected request, arg2=socket, data=the reason |

One reply per request, following fs's old contract: same type, arg0 carries the number, a negative is a no. Three new contracts were forced out by this chapter: OPEN's reply carries arg1 (the local port), because one app opens several sockets at once and the replies come back in no fixed order — **the reply has to say for itself which one it is**; the replies of RECV and SEND carry arg2 (the socket number), for the same reason — the reply arrives first, the name of the request is long forgotten, and guessing is how accidents happen; the rejection letter is the same, arg1 carrying the rejected request, arg2 carrying its socket — a bare "no" falling into a busy event loop, without a name there is nothing to trace it back from.

RECV and ACCEPT have no "no data yet, ask again later" return: the caller sleeps on the socket together with its message, and is only woken when traffic arrives. The parked style is the same one as the kernel's irq wait queue — each socket has only one seat, and a second RECV that comes knocking is told straight out "there is already someone here". No timeout: if the peer never comes, the caller sleeps on. This gap is left on purpose; it is written in the comments.

Active opens recognize only the gateway. `ip_tx`'s ARP table holds one single name, and packets sent to others vanish without a sound, so OPEN is refused outright for any address beyond the gateway, with the rejection letter carrying the reason exactly as it is; what netsh prints is
`net says no: open: only the gateway is reachable (t=1-)`, where after `t=` come the rejected request (OPEN=1) and its socket. **A limitation spoken out loud is a limitation; hidden away, it is a trap.**

## netsh: the first customer

netsh is an event loop: at boot it sends three OPENs — one outbound TCP to port 2324 on the host machine to deliver a greeting, one listening on 2323 for TCP echo, one on 2325 for UDP echo — then waits on the spot for replies, handling whatever comes. Echo means SENDing back the bytes that RECV received; the greeting is CLOSEd the moment it is sent.

Two details were bought with debugging. First, this stack's listener transforms into that connection the moment the first SYN arrives (one socket one connection), so every time a connection ends, netsh reopens a new listener to fill the seat, instead of ACCEPTing on the old socket again. Second, UDP has no such thing as "the peer ended", so the echo's send-and-receive loop sorts by the socket number in the RECV reply instead of guessing from "who the previous RECV was addressed to" — **in an event loop, any ownership kept by memory will end up on the wrong head sooner or later.**

## Verification: nc is the acceptance

Three sentences on the host machine:

```sh
nc -l -p 2324 &                              # wait for the greeting
printf 'hello net\n' | nc -N 127.0.0.1 2323  # TCP echo, answers on enter
printf 'ping udp\n'  | nc -u -w2 127.0.0.1 2325
```

`-N` is not for show: once stdin reaches EOF, nc has to half-close the write end; the kernel answers FIN, nc replies with the ACK, and only then has the connection been walked to the end. An nc without `-N` waits on and on, and what it waits for is a timeout, not the kernel's fault. On the serial port you should see `netsh: tcp echo on 2323` and, for every connection, `netsh: peer closed`. You can try it empty-handed too: with no listener placed on 2324, the greeting's connection retries eight times and gives up, and the serial port prints `net: connect gave up` — **failure has to make a sound; that is this kernel's rule.**

## Files in this chapter

```
servers/net/net.c   e1000 driver, ARP/IP/ICMP/UDP, the minimal TCP, the socket table and message replies
servers/net/net.h   the message protocol, e1000 registers, Ethernet and ip constants
apps/netsh/netsh.c   the event loop: two echoes + one outbound greeting
kernel/string.c/.h  memmove: compacting the socket receive buffer
```
