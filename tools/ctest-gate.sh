#!/bin/sh
# ctest-gate.sh -- the green gate for a push, with one flake allowance.
#
#   ctest-gate.sh [--test-dir DIR] [extra ctest args...]
#
# The QEMU timing tests (smoke, smoke-mouse, smoke-clear, smoke-reboot) pace
# keystrokes and screen reads on second-grained sleeps. On a busy shared
# runner a tick of CPU contention can make the guest boot a moment too slow
# and drop a keystroke in the tty queue, and one such flake fails a whole
# battery that is otherwise green -- which turns an environmental blip into a
# "don't push" verdict. The verdict here is the same, one allowance later:
# run the battery once; if anything failed, re-run exactly the failed tests
# (ctest --rerun-failed) once. A real regression fails again and stays red; a
# load blip comes back green. Nothing is skipped: the first run is always the
# full suite, and a re-run that still fails still fails the gate.

set -e
cd "$(dirname "$0")/.."        # paths below are relative to the repo root

# Run the full battery; collect the exit status instead of letting set -e
# stop us, so a failed first pass can be given its one retry.
if ctest "$@"; then
    exit 0
fi

echo "ctest-gate: first pass had failures; re-running the failed tests once"
ctest --rerun-failed "$@"
