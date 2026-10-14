#!/bin/sh
# smoke-clear.sh -- drive `clear` over the PS/2 keyboard and read the
# screen back out of the VGA text buffer.
#
# `clear` is the one shell command that says nothing on the serial
# port: it is a message to the tty task, which calls the console's
# SYS_CLEAR and wipes 0xb8000. Nothing comes back, so the only
# evidence is what is no longer on the screen. Hence two dumps: once
# with the boot log and a command answer on it, once after `clear`.
#
# usage: smoke-clear.sh <qemu> <kernel> <image> <outdir>

qemu="$1"; kernel="$2"; img="$3"; out="$4"
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/monitor.sock" "$out/vga-before.bin" "$out/vga-after.bin"

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
keys() { for k in "$@"; do mon "sendkey $k"; sleep 0.3; done; }

# 4000 bytes is the 80x25 text buffer; strip the per-character
# attribute bytes and lay the screen out one row per line
dump() {
    mon "pmemsave 0xb8000 4000 \"$out/$1\""
    sleep 1
    od -An -v -tu1 "$out/$1" | tr -s ' \n' '\n\n' | grep -v '^$' \
        | awk 'NR % 2 == 1 { printf "%c", $1 }' | fold -w 80 > "$out/$1.txt"
}

keys m e m ret                           # an answer on screen
sleep 2
dump vga-before.bin

keys c l e a r ret                       # and then an empty screen
sleep 2
dump vga-after.bin

kill -- -$QPID 2>/dev/null
wait $QPID 2>/dev/null

[ -f "$out/vga-before.bin" ] && [ -f "$out/vga-after.bin" ] \
    || { echo 'FAIL: no VGA dump'; exit 1; }

fail=0
# before: the keystrokes reached the shell and the shell answered
grep -Eq 'frames: [0-9]+ of [0-9]+ free' "$out/vga-before.bin.txt" \
    || { echo 'FAIL: the shell never answered on screen'; fail=1; }
grep -q 'pomelo booting' "$out/vga-before.bin.txt" \
    || { echo 'FAIL: the boot banner never reached the screen'; fail=1; }

# after: the boot log and the answer are gone -- the screen was wiped
grep -q 'pomelo booting' "$out/vga-after.bin.txt" \
    && { echo 'FAIL: clear left the boot banner on screen'; fail=1; }
grep -q 'frames' "$out/vga-after.bin.txt" \
    && { echo 'FAIL: clear left the previous answer on screen'; fail=1; }
[ "$(grep -c '[^ ]' "$out/vga-after.bin.txt")" = 1 ] \
    || { echo 'FAIL: clear did not wipe the screen'; fail=1; }
# and the prompt is back at the top: the tty task lived through it
grep -qE '^pomelo> *$' "$out/vga-after.bin.txt" \
    || { echo 'FAIL: the shell did not come back after clear'; fail=1; }

[ "$fail" = 0 ] && echo "smoke-clear ok: $out"
exit $fail