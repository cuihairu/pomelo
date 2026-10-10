#!/bin/sh
# Nightly acceptance: boot pomelo twice in QEMU and collect evidence.
#
#   nightly-run.sh <qemu> <machine> <kernel> <img> <outdir> [extra qemu args...]
#
# Run one is tools/walkthrough.sh: the full command battery over the serial
# port (-nographic), transcript saved as serial.log, every check graded.
# Run two boots again with a monitor socket so we can screendump the boot
# screen. Input pacing is the same trick as tools/smoke.sh: one command
# per second, one interrupt each.
#
# Machines differ in one way that matters here: `pc` has legacy IDE, so
# ls/cat must work; `q35` wires its disk through AHCI and leaves the ATA
# ports floating, so the fs service answers every request with `fs: no
# disk`. Both count as green; a silent hang does not.

qemu="$1"; mach="$2"; kernel="$3"; img="$4"; out="$5"; shift 5
mkdir -p "$out"
SCRIPT_DIR=$(dirname "$0")

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

# --- run 1: the walkthrough battery over the serial port -----------------
sh "$SCRIPT_DIR/walkthrough.sh" "$qemu" "$kernel" "$img" "$out/serial.log" \
    -M "$mach" "$@"

# --- run 2: screendump of the boot screen --------------------------------
rm -f "$out/monitor.sock" "$out/boot.ppm"
{ pacing; } | timeout 25 "$qemu" -M "$mach" -kernel "$kernel" -hda "$img" \
    -display none -serial stdio -no-reboot "$@" \
    -netdev user,id=n0,hostfwd=tcp::2323-:2323 -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file="$out/net.pcap" \
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
