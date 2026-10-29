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
# Input is prompt-gated, not timed: the driver watches the transcript and
# types the next line only once the shell's `pomelo> ` has appeared, then
# gives the tty a second per command. Fixed early timing lost the opening
# commands on a slow boot -- the help keystroke died before the UART was
# configured and the nightly graded a shell that never saw a command.
#
# The machine is not named here: without extra args QEMU picks `pc` (legacy
# IDE, full disk battery). Pass `-M q35` for the AHCI machine -- the fs
# service then parks with a no-disk note, which this script detects in the
# transcript and grades on shell checks alone, same as the nightly ruling.

qemu="$1"; kernel="$2"; img="$3"; out="$4"; shift 4
pcap=$(dirname "$out")/net.pcap
fifo=$(dirname "$out")/walk.in
trap '' PIPE                          # a dead qemu must not kill the driver

rm -f "$fifo"
mkfifo "$fifo"
timeout 25 "$qemu" -kernel "$kernel" -hda "$img" \
    -nographic -no-reboot "$@" \
    -netdev user,id=n0,hostfwd=tcp::2323-:2323 -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file="$pcap" \
    < "$fifo" > "$out" 2>&1 &
qpid=$!

exec 3>"$fifo"                        # hold the write end: no mid-run EOF

want=0
send() {
    want=$((want + 1))
    i=0
    while [ "$(grep -c 'pomelo> ' "$out" 2>/dev/null)" -lt "$want" ] \
        && [ "$i" -lt 88 ]; do        # give up only near the timeout
        sleep 0.25; i=$((i + 1))
    done
    printf '%s\n' "$1" >&3 || return 0
    sleep 1                           # one command per tick, as before
}

send 'help'
send 'ps'
send 'spawn probe'
send 'ls'
send 'cat hello.txt'
send 'uptime'
send 'echo hello pomelo'
send 'write walk.txt hello walkthrough'
send 'ls'
send 'cat walk.txt'
send 'mem'

exec 3>&-
wait "$qpid" 2>/dev/null
rm -f "$fifo"

fail=0
check() { grep -Eq "$2" "$out" || { echo "FAIL: $1"; fail=1; }; }

check 'no boot banner'        'pomelo booting'
check 'shell never spoke'     'pomelo shell'
check 'spawn did not revive probe' 'probe is task 4 now'
check 'uptime never answered' 'up [0-9]+s'
check 'mem never answered'    'frames: [0-9]+ of [0-9]+ free'
# help's listing must match the table in shell.c, both directions: a
# command added to one and forgotten in the other is a shell that lies
root=$(cd "$(dirname "$0")/.." && pwd)
tbl=$(sed -n 's/^    { "\([a-z]*\)".*/\1/p' "$root/apps/shell/shell.c" | sort)
listed=$(grep -Eo '^  [a-z]+' "$out" | awk '{print $1}' | sort -u)
[ "$tbl" = "$listed" ] \
    || { echo 'FAIL: help listing does not match the command table'; fail=1; }
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
    check 'ls never listed the new file' 'walk.txt'
fi

[ "$fail" = 0 ] && echo "walkthrough ok: $out"
exit $fail
