#!/bin/sh
# Nightly acceptance: boot pomelo twice in QEMU and collect evidence.
#
#   nightly-run.sh <qemu> <machine> <kernel> <img> <outdir> [extra qemu args...]
#
# Run one drives the shell over the serial port (-nographic) and saves
# the transcript as serial.log. Run two boots again with a monitor
# socket so we can screendump the boot screen. Input pacing is the same
# trick as tools/smoke.sh: one command per second, one interrupt each.
#
# Machines differ in one way that matters here: `pc` has legacy IDE, so
# ls/cat must work; `q35` wires its disk through AHCI and leaves the ATA
# ports floating, so the fs service reports the absence and parks. Both
# count as green; a silent hang does not.

qemu="$1"; mach="$2"; kernel="$3"; img="$4"; out="$5"; shift 5
mkdir -p "$out"

pacing() {
    sleep 2; printf '\n'
    sleep 1; printf 'help\n'
    sleep 1; printf 'ls\n'
    sleep 1; printf 'cat hello.txt\n'
    sleep 2
}

# --- run 1: serial transcript -------------------------------------------
{ pacing; } | timeout 25 "$qemu" -M "$mach" -kernel "$kernel" -hda "$img" \
    -nographic -no-reboot "$@" > "$out/serial.log" 2>&1

grep -q 'pomelo booting' "$out/serial.log" || { echo "FAIL: no boot banner"; exit 1; }
grep -q "pomelo shell"   "$out/serial.log" || { echo "FAIL: shell never spoke"; exit 1; }
if grep -q 'no disk behind the ata ports' "$out/serial.log"; then
    echo "note: machine $mach has no legacy ide disk; shell-only checks"
else
    grep -q 'hello from the pomelo disk' "$out/serial.log" \
        || { echo "FAIL: disk read failed"; exit 1; }
fi

# --- run 2: screendump of the boot screen --------------------------------
rm -f "$out/monitor.sock" "$out/boot.ppm"
{ pacing; } | timeout 25 "$qemu" -M "$mach" -kernel "$kernel" -hda "$img" \
    -display none -serial stdio -no-reboot "$@" \
    -monitor unix:"$out/monitor.sock",server,nowait > /dev/null 2>&1 &
QPID=$!
i=0
while [ ! -S "$out/monitor.sock" ] && [ $i -lt 80 ]; do
    sleep 0.25; i=$((i + 1))
done
sleep 6                                  # let the battery paint the screen
printf 'screendump %s/boot.ppm\n' "$out" \
    | socat - unix:"$out/monitor.sock" > /dev/null 2>&1
sleep 1
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

[ -f "$out/boot.ppm" ] || { echo "FAIL: screendump never landed"; exit 1; }
if command -v convert > /dev/null; then
    convert "$out/boot.ppm" "$out/boot.png" && rm -f "$out/boot.ppm"
else
    mv "$out/boot.ppm" "$out/boot.png"   # keep the evidence, ugly or not
fi
echo "nightly run ok: $out/serial.log + $out/boot.png"
