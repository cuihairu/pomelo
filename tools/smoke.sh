#!/bin/sh
# Boot pomelo in QEMU and drive the shell over the serial port.
#
# Input is paced, not burst: the tty task processes characters one
# interrupt at a time, and a burst would overflow its line buffer
# before the shell could ask for a line. The first byte after boot
# is swallowed by the UART handshake, so a newline primes the port.
#
# usage: smoke.sh <qemu> <kernel> <image> <output-log>

qemu="$1"; kernel="$2"; img="$3"; out="$4"

{ sleep 2; printf '\n'
  sleep 1; printf 'help\n'
  sleep 1; printf 'ps\n'
  sleep 1; printf 'spawn probe\n'
  sleep 1; printf 'ls\n'
  sleep 1; printf 'cat hello.txt\n'
  sleep 2; } | timeout 12 "$qemu" -kernel "$kernel" -hda "$img" \
      -serial stdio -display none -no-reboot > "$out" 2>&1

grep -q 'hello from the pomelo disk' "$out"
# the ring 3 probe: alive, then rightly killed for touching kernel memory;
# then the shell revives it by name, and it dies all over again
grep -q 'probe is task 4 now' "$out"
[ "$(grep -c 'probe: alive at ring 3' "$out")" = 2 ]
[ "$(grep -c 'killed: page fault' "$out")" = 2 ]
