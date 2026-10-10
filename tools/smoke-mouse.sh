#!/bin/sh
# smoke-mouse.sh -- the same evidence procedure smoke-ps2.sh uses for the
# keyboard, pointed at the PS/2 mouse instead. The QEMU monitor's
# mouse_move / mouse_button events travel the real path: emulated 8042
# aux channel -> kernel/mouse.c isr -> vga block cursor. The verdict is
# read out of the 0xb8000 text buffer the same way: a cell the cursor
# occupies carries the inverted attribute 0x70, a cell it left is back
# to the default 0x07. Nothing about the mouse path is visible without
# this -- motion that never lands is indistinguishable from a dead isr.
#
# usage: smoke-mouse.sh <qemu> <kernel> <image> <outdir>

qemu="$1"; kernel="$2"; img="$3"; out="$4"
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/monitor.sock" "$out"/vga*.bin

# own session: the group kill below takes out timeout AND qemu
setsid timeout 60 "$qemu" -kernel "$kernel" -hda "$img" \
    -display none -serial null -no-reboot \
    -monitor unix:"$out/monitor.sock",server,nowait > "$out/qemu.log" 2>&1 &
QPID=$!

i=0
while [ ! -S "$out/monitor.sock" ] && [ $i -lt 80 ]; do
    sleep 0.25; i=$((i + 1))
done
sleep 4                                  # boot, banner, mouse init

mon() { printf '%s\n' "$1" | socat - "unix:$out/monitor.sock" > /dev/null; }

# the cursor is parked at (40,12) once mouse_init finishes. Three moves
# with a button press in between; each move is followed by a dump.
mon "mouse_move 15 6"                    # (40,12) -> (55,18)
sleep 1
mon "pmemsave 0xb8000 4000 \"$out/vga1.bin\""
mon "mouse_move -5 -2"                   # (55,18) -> (50,16)
sleep 1
mon "pmemsave 0xb8000 4000 \"$out/vga2.bin\""
mon "mouse_button 1"                     # left button down, then up:
sleep 0.3                                # motion this sample reports is
mon "mouse_button 0"                     # zero, and the block stays put
sleep 0.3
mon "mouse_move 3 3"                    # (50,16) -> (53,19)
sleep 1
mon "pmemsave 0xb8000 4000 \"$out/vga3.bin\""
sleep 1
kill -- -$QPID 2>/dev/null
wait $QPID 2>/dev/null

for n in 1 2 3; do
    [ -f "$out/vga$n.bin" ] || { echo "FAIL: no VGA dump $n"; exit 1; }
done

# attribute byte of cell (col,row): offset (row*80+col)*2+1 in the dump
attr() { od -An -tu1 -j $(( ($2 * 80 + $1) * 2 + 1 )) -N 1 "$3" | tr -d ' '; }

# 0x70 = the inverted block cursor, 0x07 = the plain cell underneath
[ "$(attr 55 18 "$out/vga1.bin")" = "112" ] \
    || { echo "FAIL: cursor never reached (55,18)"; exit 1; }
[ "$(attr 50 16 "$out/vga2.bin")" = "112" ] \
    || { echo "FAIL: cursor never reached (50,16)"; exit 1; }
[ "$(attr 55 18 "$out/vga2.bin")" = "7" ] \
    || { echo "FAIL: the cell it left was not restored"; exit 1; }
[ "$(attr 53 19 "$out/vga3.bin")" = "112" ] \
    || { echo "FAIL: the button events derailed the mouse path"; exit 1; }
[ "$(attr 50 16 "$out/vga3.bin")" = "7" ] \
    || { echo "FAIL: the cell it left was not restored"; exit 1; }
echo "smoke-mouse ok: $out"
