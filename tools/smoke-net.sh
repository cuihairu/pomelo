#!/bin/sh
# smoke-net.sh -- boot pomelo on a slirp NIC and drive its socket service
# with netcat from the host. The guest runs netsh: a tcp echo on 2323,
# a udp echo on 2325, and one outbound greeting to 2324. Every check
# asserts the bytes that came back, not just an exit code -- a hung
# netcat exits 124, a refused one exits 1, and both would look the same
# in a log.
#
# usage: smoke-net.sh <qemu> <kernel> <image> <outdir>

qemu="$1"; kernel="$2"; img="$3"; out="$4"
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/smoke.log" "$out/greeting.txt" "$out/tcp.txt" "$out/tcp2.txt" \
      "$out/udp.txt"

# own session: the group kill below takes out timeout AND qemu
setsid timeout 60 "$qemu" -kernel "$kernel" -hda "$img" \
    -display none -no-reboot -serial file:"$out/smoke.log" \
    -netdev user,id=n0,hostfwd=tcp::2323-:2323,hostfwd=udp::2325-:2325 \
    -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file="$out/net.pcap" \
    > "$out/qemu.log" 2>&1 &
QPID=$!

# the host's ear first: netsh greets about a second after boot
nc -l -p 2324 > "$out/greeting.txt" 2>/dev/null &
ear=$!
# both die on every exit: a leftover listener on 2324 would steal the next
# run's greeting, and the test would fail on the wrong evidence
trap 'kill -- -$QPID 2>/dev/null; wait $QPID 2>/dev/null; kill $ear 2>/dev/null' EXIT

# wait for the listener to announce itself; the timeout is the fail path
i=0
while ! grep -q "tcp echo on 2323" "$out/smoke.log" 2>/dev/null; do
    sleep 0.25; i=$((i + 1))
    [ $i -gt 80 ] && {
        echo "FAIL: netsh never announced a listener"
        grep -a "PANIC\|terminating\|Could not" "$out/qemu.log" 2>/dev/null | head -2
        grep -a "PANIC" "$out/smoke.log" 2>/dev/null | head -2
        exit 1
    }
done
sleep 1

# -N makes netcat half-close after stdin eof, so the connection can finish
printf 'hello net\n' | nc -N 127.0.0.1 2323 > "$out/tcp.txt"; st=$?
[ $st -eq 0 ] || { echo "FAIL: tcp echo on 2323, netcat exited $st"; exit 1; }
grep -q 'hello net' "$out/tcp.txt" \
    || { echo "FAIL: the tcp echo sent nothing back"; exit 1; }

# a second call: the listener became the first connection, so netsh has
# to open a fresh one -- this is the accept loop
printf 'second call\n' | nc -N 127.0.0.1 2323 > "$out/tcp2.txt"; st=$?
[ $st -eq 0 ] || { echo "FAIL: the second tcp call, netcat exited $st"; exit 1; }
grep -q 'second call' "$out/tcp2.txt" \
    || { echo "FAIL: the second listener never answered"; exit 1; }

printf 'ping udp\n' | nc -u -w2 127.0.0.1 2325 > "$out/udp.txt"
grep -q 'ping udp' "$out/udp.txt" \
    || { echo "FAIL: the udp echo on 2325 sent nothing back"; exit 1; }

kill $ear 2>/dev/null
grep -q 'hello from pomelo' "$out/greeting.txt" \
    || { echo "FAIL: the outbound greeting never reached 2324"; exit 1; }

# a clean tear-down, twice, with the guest's own words on the wire
grep -c "peer closed" "$out/smoke.log" | grep -q '^2$' \
    || { echo "FAIL: the tcp closes did not reach netsh"; exit 1; }

echo "smoke-net ok: $out/smoke.log"
