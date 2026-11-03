#!/bin/sh
# unit-mkfs: drive the real host mkfs and assert the reachable guards.
#   Each assertion gets its own directory and image so it cannot depend
#   on the host filesystem's readdir order.
#   - a file larger than MAXFILE is truncated to MAXFILE in the table
#   - the seed log reports the truncated size (not the source size)
#   - NINODES=32 caps the file count first, so image-full is not
#     reachable here (32*6=192 blocks < NDATA=1024); "too many files"
#     is the reachable stop.
set -e
MKFS="$1"
[ -x "$MKFS" ] || { echo "FAIL: mkfs not found: $MKFS"; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/truncate/seed"
head -c 4000 /dev/zero > "$TMP/truncate/seed/big"
"$MKFS" "$TMP/truncate/img" "$TMP/truncate/seed" \
    > "$TMP/truncate/log" 2>&1
grep -q 'seeded /big (3072 bytes)' "$TMP/truncate/log" \
    || { echo "FAIL: big file not truncated/reported as 3072"; \
         cat "$TMP/truncate/log"; exit 1; }

mkdir -p "$TMP/cap/seed"
i=0
while [ "$i" -lt 35 ]; do
    head -c 3072 /dev/zero > "$TMP/cap/seed/f$i"
    i=$((i + 1))
done
"$MKFS" "$TMP/cap/img" "$TMP/cap/seed" > "$TMP/cap/log" 2>&1
# NINODES caps the file count first, so image full never triggers
grep -q 'too many files' "$TMP/cap/log" \
    || { echo "FAIL: NINODES cap not reached"; cat "$TMP/cap/log"; exit 1; }
echo "unit-mkfs: all ok"
