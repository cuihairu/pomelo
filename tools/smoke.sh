#!/bin/sh
# Boot pomelo in QEMU and drive the shell over the serial port.
#
# Input is prompt-gated, not timed: the driver watches the transcript and
# types each line only once the shell's `pomelo> ` has reappeared, so a
# slow boot -- or a load-saturated host -- never loses a keystroke to a
# fixed sleep. The first byte after boot is still swallowed by the UART
# handshake, so a newline primes the port before the battery. Characters
# within a line stay paced: the tty task takes one character per
# interrupt, and a burst would overflow its 16-byte UART FIFO.
#
# usage: smoke.sh <qemu> <kernel> <image> <output-log>

qemu="$1"; kernel="$2"; img="$3"; out="$4"
pcap=$(dirname "$out")/net.pcap
fifo=$(dirname "$out")/smoke.in
netargs="-netdev user,id=n0,hostfwd=tcp::2323-:2323 -device e1000,netdev=n0
-object filter-dump,id=f0,netdev=n0,file=$pcap"
trap '' PIPE                          # a dead qemu must not kill the driver

: > "$out"
rm -f "$fifo"
mkfifo "$fifo"
timeout 45 "$qemu" -kernel "$kernel" -hda "$img" \
    -serial stdio -display none -no-reboot $netargs \
    < "$fifo" > "$out" 2>&1 &
qpid=$!
# O_RDWR open never waits on a reader: a qemu that dies before opening the
# read end must not hang the driver. The write end is still held, so the
# fifo sees no mid-run EOF.
exec 3<>"$fifo"

# Unanchored count: a slow boot interleaves the net banner with the prompt
# ("net: e1000 up, mac pomelo> "), and the prompt is what proves the shell
# is idle, glued or not.
prompts() { grep -c 'pomelo> ' "$out" 2>/dev/null || echo 0; }

# wait until the transcript shows at least $1 prompts, or 30s pass
wait_prompt() {
    deadline=$(( $(date +%s) + 30 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        [ "$(prompts)" -ge "$1" ] && return 0
        sleep 0.25
    done
    return 1
}

# Type one line, then wait for the shell to come back idle: the prompt
# count we read before typing is the idle point, and this line adds one
# more. Typing never runs more than one line ahead of the shell, so the
# 16-byte UART FIFO cannot overflow; if the shell has not spoken yet the
# chars queue in it until it does.
send() {
    base=$(prompts)
    i=1; len=$(printf '%s' "$1" | wc -c)
    while [ "$i" -le "$len" ]; do
        printf '%s' "$(printf '%s' "$1" | cut -c"$i")" >&3
        sleep 0.02; i=$((i + 1))
    done
    printf '\n' >&3
    wait_prompt $((base + 1)) || return 0   # shell never spoke in time: skip
}

wait_prompt 1 || true                 # the shell spoke at least once
printf '\n' >&3                       # prime: the first byte is swallowed

send 'help'
send 'ps'
send 'spawn probe'
send 'ls'
send 'cat hello.txt'
send 'uptime'
send 'echo hello pomelo'
send 'mem'

exec 3>&-
kill "$qpid" 2>/dev/null      # the battery is done; don't idle to the timeout
wait "$qpid" 2>/dev/null
rm -f "$fifo"

fail=0
check() { grep -Eq "$2" "$out" || { echo "FAIL: $1"; fail=1; }; }

check 'boot banner never appeared' 'pomelo booting'
check 'shell never spoke'          'pomelo shell'
check 'e1000 never came up'        'net: e1000 up'
check 'netsh never announced a listener' 'netsh: tcp echo on 2323'
check 'ls never listed the seeds'  'hello.txt'
check 'cat never answered'         'hello from the pomelo disk'
check 'echo never came back'       'hello pomelo'
check 'mem never answered'         'frames: [0-9]+ of [0-9]+ free'
check 'spawn did not revive probe' 'probe is task 4 now'
check 'uptime never answered'      'up [0-9]+s'
# help's listing must match the table in shell.c, both directions: a
# command added to one and forgotten in the other is a shell that lies
root=$(cd "$(dirname "$0")/.." && pwd)
tbl=$(sed -n 's/^    { "\([a-z]*\)".*/\1/p' "$root/apps/shell/shell.c" | sort)
listed=$(grep -Eo '^  [a-z]+' "$out" | awk '{print $1}' | sort -u)
[ "$tbl" = "$listed" ] \
    || { echo 'FAIL: help listing does not match the command table'; fail=1; }
# the ring 3 probe: alive, then rightly killed for touching kernel memory;
# then the shell revives it by name, and it dies all over again
[ "$(grep -c 'probe: alive at ring 3' "$out")" = 2 ] \
    || { echo 'FAIL: probe never spoke twice'; fail=1; }
[ "$(grep -c 'killed: page fault' "$out")" = 2 ] \
    || { echo 'FAIL: probe was not killed twice'; fail=1; }

[ "$fail" = 0 ] && echo "smoke ok: $out"
exit $fail
