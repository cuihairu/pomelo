#!/bin/sh
# Machine walkthrough: boot pomelo and drive the full command battery over
# the serial port, then grade the transcript line by line.
#
#   walkthrough.sh <qemu> <kernel> <image> <output-log> [extra qemu args...]
#
# This is the acceptance tour smoke.sh does not cover: on top of smoke's
# battery it creates a file (write), lists it, reads it back -- the round
# trip that hung the shell before sys_send_wait -- and it is the same script
# tools/nightly-run.sh runs as its first boot.
#
# Input is paced, not burst: the tty task processes characters one interrupt
# at a time, and a burst would overflow its line buffer before the shell
# could ask for a line. The first byte after boot is swallowed by the UART
# handshake, so a newline primes the port.
#
# The machine is not named here: without extra args QEMU picks `pc` (legacy
# IDE, full disk battery). Pass `-M q35` for the AHCI machine -- the fs
# service then parks with a no-disk note, which this script detects in the
# transcript and grades on shell checks alone, same as the nightly ruling.

qemu="$1"; kernel="$2"; img="$3"; out="$4"; shift 4
pcap=$(dirname "$out")/net.pcap

pacing() {
    sleep 2; printf '\n'
    sleep 1; printf 'help\n'
    sleep 1; printf 'ps\n'
    sleep 1; printf 'spawn probe\n'
    sleep 1; printf 'ls\n'
    sleep 1; printf 'cat hello.txt\n'
    sleep 1; printf 'uptime\n'
    sleep 1; printf 'echo hello pomelo\n'
    sleep 1; printf 'write walk.txt hello walkthrough\n'
    sleep 1; printf 'ls\n'
    sleep 1; printf 'cat walk.txt\n'
    sleep 1; printf 'mem\n'
    sleep 2
}

{ pacing; } | timeout 25 "$qemu" -kernel "$kernel" -hda "$img" \
    -nographic -no-reboot "$@" \
    -netdev user,id=n0,hostfwd=tcp::2323-:2323 -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file="$pcap" > "$out" 2>&1

fail=0
check() { grep -Eq "$2" "$out" || { echo "FAIL: $1"; fail=1; }; }

check 'no boot banner'        'pomelo booting'
check 'shell never spoke'     'pomelo shell'
check 'spawn did not revive probe' 'probe is task 4 now'
check 'uptime never answered' 'up [0-9]+s'
check 'mem never answered'    'frames: [0-9]+ of [0-9]+ free'
[ "$(grep -c 'probe: alive at ring 3' "$out")" = 2 ] \
    || { echo 'FAIL: probe never spoke twice'; fail=1; }
[ "$(grep -c 'killed: page fault' "$out")" = 2 ] \
    || { echo 'FAIL: probe was not killed twice'; fail=1; }

if grep -q 'no disk behind the ata ports' "$out"; then
    echo 'note: no legacy ide disk on this machine; shell-only checks'
    check 'fs did not answer no' 'fs: no disk'
else
    check 'disk read failed'     'hello from the pomelo disk'
    check 'write never answered' 'wrote 18 bytes to walk.txt'
    check 'cat of a new file never answered' '^hello walkthrough$'
fi

[ "$fail" = 0 ] && echo "walkthrough ok: $out"
exit $fail
