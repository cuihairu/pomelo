#!/bin/sh
# docs-chapters.sh -- snapshot the built documentation site pages, then diff a
# rebuild against the snapshot.
#
# The vitepress 2.x upgrade (docs/todo.md L5) has to land with "the 16
# chapters show no regression". Eyeballing 34 built pages is not a gate, so
# this script stores the pages built with the currently pinned dependencies
# and checks a later build against them. Asset filenames carry content hashes
# and every build rewrites them, so assets/* references are normalized away --
# the diff is for chapter content drift, not for chunk renaming.
#
# usage: tools/docs-chapters.sh save | check
#   save   build the site and store the 34 site pages under build/docs-baseline
#   check  build the site and diff every page against the baseline;
#          exit 1 on any drift, 2 when there is no baseline to check against

set -e

mode="$1"
base="build/docs-baseline"
dist="docs/.vitepress/dist"

if [ "$mode" != save ] && [ "$mode" != check ]; then
    echo "usage: $0 save|check" >&2
    exit 2
fi

pnpm --prefix docs run build > /dev/null

if [ "$mode" = save ]; then
    rm -rf "$base"
    mkdir -p "$base/guide" "$base/zh/guide"
    cp "$dist/index.html" "$base/index.html"
    cp "$dist/zh/index.html" "$base/zh/index.html"
    cp "$dist"/guide/*.html "$base/guide/"
    cp "$dist"/zh/guide/*.html "$base/zh/guide/"
    echo "docs-chapters ok: saved 34 site pages to $base"
    exit 0
fi

if [ ! -f "$base/index.html" ]; then
    echo "FAIL: no baseline under $base -- run '$0 save' with the old deps first"
    exit 2
fi

norm() {
    sed 's#assets/[^"]*#assets/#g' "$1"
}

tmp1="build/.docs-chapters.norm1"
tmp2="build/.docs-chapters.norm2"
fail=0
count=0
for f in "$base"/index.html "$base"/zh/index.html \
         "$base"/guide/*.html "$base"/zh/guide/*.html; do
    rel=${f#"$base"/}
    count=$((count + 1))
    if [ ! -f "$dist/$rel" ]; then
        echo "FAIL: $rel missing from the new build"
        fail=1
        continue
    fi
    norm "$f" > "$tmp1"
    norm "$dist/$rel" > "$tmp2"
    if ! cmp -s "$tmp1" "$tmp2"; then
        echo "FAIL: $rel drifted from the baseline"
        fail=1
    fi
done

newpages=0
for f in "$dist"/index.html "$dist"/zh/index.html \
         "$dist"/guide/*.html "$dist"/zh/guide/*.html; do
    rel=${f#"$dist"/}
    [ -f "$base/$rel" ] || { echo "FAIL: $rel is new, absent from the baseline"; fail=1; }
    newpages=$((newpages + 1))
done

echo "docs-chapters: checked $count baseline pages against $newpages built pages"
if [ "$fail" = 0 ]; then
    echo "docs-chapters ok: no regression"
fi
exit $fail
