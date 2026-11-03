#!/bin/sh
# unit-mkfs: drive the real host mkfs and assert the reachable guards.
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
mkdir -p "$TMP/seed"
head -c 4000 /dev/zero > "$TMP/seed/big"
i=0
while [ "$i" -lt 35 ]; do
    head -c 3072 /dev/zero > "$TMP/seed/f$i"
    i=$((i + 1))
done
"$MKFS" "$TMP/img" "$TMP/seed" > "$TMP/log" 2>&1
# the big file must be truncated in the table and reported so
grep -q 'seeded /big (3072 bytes)' "$TMP/log" \
    || { echo "FAIL: big file not truncated/reported as 3072"; cat "$TMP/log"; exit 1; }
# NINODES caps the file count first, so image full never triggers
grep -q 'too many files' "$TMP/log" \
    || { echo "FAIL: NINODES cap not reached"; cat "$TMP/log"; exit 1; }
echo "unit-mkfs: all ok"
