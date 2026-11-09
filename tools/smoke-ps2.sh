#!/bin/sh
# smoke-ps2.sh -- boot pomelo with NO host serial input and type on the
# emulated PS/2 keyboard instead. Every other test drives the shell over
# COM1; this one exercises the other console input path, kernel/char.c's
# kbd_isr and its scan-code map, and reads the answer back out of the
# VGA text buffer at 0xb8000 (char bytes at even offsets).
#
# The keystrokes and the answer are gated on what is actually on screen,
# not on a fixed sleep: a slow boot (or a load-saturated host) would have
# the keys land before the shell was up, or the dump before the answer.
#
# usage: smoke-ps2.sh <qemu> <kernel> <image> <outdir>

qemu="$1"; kernel="$2"; img="$3"; out="$4"
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/monitor.sock" "$out/vga.bin" "$out/vga.txt" \
      "$out/vga.tmp.bin" "$out/vga.tmp.txt"

# own session: the group kill below takes out timeout AND qemu
setsid timeout 90 "$qemu" -kernel "$kernel" -hda "$img" \
    -display none -serial null -no-reboot \
    -netdev user,id=n0,hostfwd=tcp::2323-:2323 -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file="$out/net.pcap" \
    -monitor unix:"$out/monitor.sock",server,nowait > "$out/qemu.log" 2>&1 &
QPID=$!

i=0
while [ ! -S "$out/monitor.sock" ] && [ $i -lt 80 ]; do
    sleep 0.25; i=$((i + 1))
done

mon() { printf '%s\n' "$1" | socat - "unix:$out/monitor.sock" > /dev/null; }

# dump the screen into $1 (one 80-column row per line) via the monitor
vga_dump() {
    mon "pmemsave 0xb8000 4000 \"$out/vga.tmp.bin\""
    od -An -v -tu1 "$out/vga.tmp.bin" | tr -s ' \n' '\n\n' | grep -v '^$' \
        | awk 'NR % 2 == 1 { printf "%c", $1 }' | fold -w 80 > "$1"
}

# wait until the screen matches $1; $2 tries of 0.25s
vga_wait() {
    i=0
    while [ $i -lt "$2" ]; do
        vga_dump "$out/vga.tmp.txt"
        grep -qE "$1" "$out/vga.tmp.txt" && return 0
        sleep 0.25; i=$((i + 1))
    done
    return 1
}

# wait until two dumps 0.5s apart match ($1 tries): the console is idle,
# so no boot-time writer (net's banners, netsh's greeting) can interleave
# with the echo of the keystrokes that follow and split it across rows
vga_settled() {
    i=0
    while [ $i -lt "$1" ]; do
        vga_dump "$out/vga.a.txt"
        sleep 0.5
        vga_dump "$out/vga.b.txt"
        if cmp -s "$out/vga.a.txt" "$out/vga.b.txt"; then return 0; fi
        i=$((i + 1))
    done
    return 1
}

# the shell's prompt, on screen, before any keystroke
if ! vga_wait 'pomelo>' 120; then
    echo "FAIL: the shell never spoke on screen"
    kill -- -$QPID 2>/dev/null; exit 1
fi
# the screen still: a keystroke typed into a busy console can be glued to
# another task's line, so the echo and the answer stay separated
if ! vga_settled 120; then
    echo "FAIL: the console never went quiet"
    kill -- -$QPID 2>/dev/null; exit 1
fi

# one command, keystroke by keystroke: mem <enter>
for k in m e m ret; do
    mon "sendkey $k"
    sleep 0.3
done

# the answer, still on screen: mem's reply arrives after the tty -> shell
# -> VGA round trip, and -serial null leaves no other way in
if ! vga_wait 'frames: [0-9]+ of [0-9]+ free' 120; then
    echo "FAIL: the shell never answered on screen"
    kill -- -$QPID 2>/dev/null; exit 1
fi

vga_dump "$out/vga.txt"
kill -- -$QPID 2>/dev/null
wait $QPID 2>/dev/null
rm -f "$out/vga.tmp.bin" "$out/vga.tmp.txt" "$out/vga.a.txt" "$out/vga.b.txt"

[ -f "$out/vga.txt" ] || { echo "FAIL: no VGA dump"; exit 1; }

# The answer proves the whole path end to end: nothing prints "frames:" but
# the shell's mem command, and mem only runs on the keystrokes we sent --
# with -serial null there is no other way in. A separate echo-row check is
# deliberately absent: a concurrent console writer (netsh's greeting) can
# split the echoed keystrokes across screen rows, so a row-level echo
# assertion is racy by construction and adds no coverage over the answer.
grep -Eq 'frames: [0-9]+ of [0-9]+ free' "$out/vga.txt" \
    || { echo "FAIL: the shell never answered on screen"; exit 1; }
echo "smoke-ps2 ok: $out/vga.txt"
