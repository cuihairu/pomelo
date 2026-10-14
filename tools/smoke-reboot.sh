#!/bin/sh
# smoke-reboot.sh -- the last command of the battery has to take the
# machine down with it.
#
# `reboot` is the one shell command that never answers: the shell
# writes its notice straight to the kernel console (a tty message
# would still be sitting in the queue when the reset lands), then
# SYS_REBOOT pulses the keyboard controller reset line, 0x64 <- 0xfe.
# The transcript can only show the notice, so the real evidence is
# the exit status: QEMU runs with -no-reboot, so a reset that landed
# ends the process with status 0, while a reset that never landed
# leaves QEMU booting until `timeout` kills it (124).
#
# usage: smoke-reboot.sh <qemu> <kernel> <image> <output-log>

qemu="$1"; kernel="$2"; img="$3"; out="$4"

{ sleep 2; printf '\n'
  sleep 1; printf 'reboot\n'
  sleep 3; } | timeout 20 "$qemu" -kernel "$kernel" -hda "$img" \
      -serial stdio -display none -no-reboot > "$out" 2>&1
status=$?

fail=0
grep -q 'rebooting\.\.\.' "$out" \
    || { echo 'FAIL: reboot never announced itself'; fail=1; }
if [ "$status" != 0 ]; then
    echo "FAIL: qemu exited $status, not 0 -- the reset never landed"
    fail=1
fi

[ "$fail" = 0 ] && echo "smoke-reboot ok: $out"
exit $fail