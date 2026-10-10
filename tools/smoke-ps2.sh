#!/bin/sh
# smoke-ps2.sh -- boot pomelo with NO host serial input and type on the
# emulated PS/2 keyboard instead. Every other test drives the shell over
# COM1; this one exercises the other console input path, kernel/char.c's
# kbd_isr and its scan-code map, and reads the answer back out of the
# VGA text buffer at 0xb8000 (char bytes at even offsets).
#
# usage: smoke-ps2.sh <qemu> <kernel> <image> <outdir>

qemu="$1"; kernel="$2"; img="$3"; out="$4"
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/monitor.sock" "$out/vga.bin" "$out/vga.txt"

# own session: the group kill below takes out timeout AND qemu
setsid timeout 60 "$qemu" -kernel "$kernel" -hda "$img" \
    -display none -serial null -no-reboot \
    -monitor unix:"$out/monitor.sock",server,nowait > "$out/qemu.log" 2>&1 &
QPID=$!

i=0
while [ ! -S "$out/monitor.sock" ] && [ $i -lt 80 ]; do
    sleep 0.25; i=$((i + 1))
done
sleep 4                                  # boot, banner, shell prompt

mon() { printf '%s\n' "$1" | socat - "unix:$out/monitor.sock" > /dev/null; }

# one command, keystroke by keystroke: mem <enter>
for k in m e m ret; do
    mon "sendkey $k"
    sleep 0.3
done
sleep 2                                  # tty -> shell -> answer -> VGA

# the monitor parses an unquoted /path as arithmetic; quote it
mon "pmemsave 0xb8000 4000 \"$out/vga.bin\""
sleep 1
kill -- -$QPID 2>/dev/null
wait $QPID 2>/dev/null

[ -f "$out/vga.bin" ] || { echo "FAIL: no VGA dump"; exit 1; }

# strip the per-character attribute bytes: keep every even offset, one
# 80-column screen row per line
od -An -v -tu1 "$out/vga.bin" | tr -s ' \n' '\n\n' | grep -v '^$' \
    | awk 'NR % 2 == 1 { printf "%c", $1 }' | fold -w 80 > "$out/vga.txt"

# the typed echo, on its own row (screen rows pad out to 80 columns)
grep -qE '^mem *$' "$out/vga.txt" \
    || { echo "FAIL: PS/2 keystrokes never reached the shell"; exit 1; }
# the answer: with -serial null there is no other way in
grep -Eq 'frames: [0-9]+ of [0-9]+ free' "$out/vga.txt" \
    || { echo "FAIL: the shell never answered on screen"; exit 1; }
echo "smoke-ps2 ok: $out/vga.txt"
