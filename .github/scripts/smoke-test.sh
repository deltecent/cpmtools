#!/bin/sh
# Round-trip smoke test for the cpmtools binaries in directory $1 (default .).
# Run it from the source tree so the tools pick up ./diskdefs. Set EXEEXT=.exe
# on Windows.
#
# For each format: make a file system, copy binary data in (every byte value,
# so text-mode translation would show), list it, copy it back out, compare,
# check the file system, remove the file and check again.
set -eu

bin=${1:-.}
exe=${EXEEXT:-}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

run() {
    tool=$1; shift
    "$bin/$tool$exe" "$@"
}

# 256 byte values, repeated to span several blocks and extents
i=0
while [ $i -lt 256 ]; do
    printf "\\$(printf %03o $i)"
    i=$((i+1))
done > "$work/bytes"
: > "$work/data.bin"
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 \
         21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do
    cat "$work/bytes" >> "$work/data.bin"
done

for fmt in ibm-3740 altair; do
    echo "== $fmt"
    img=$work/$fmt.img
    run mkfs.cpm -f "$fmt" "$img"
    run cpmcp -f "$fmt" "$img" "$work/data.bin" 0:data.bin
    run cpmls -f "$fmt" -l "$img"
    run cpmcp -f "$fmt" "$img" 0:data.bin "$work/out.bin"
    cmp "$work/data.bin" "$work/out.bin"
    rm -f "$work/out.bin"
    run cpmchattr -f "$fmt" "$img" r 0:data.bin
    run cpmchattr -f "$fmt" "$img" n 0:data.bin
    run fsck.cpm -f "$fmt" -n "$img"
    run cpmrm -f "$fmt" "$img" 0:data.bin
    if [ -n "$(run cpmls -f "$fmt" "$img")" ]; then
        echo "error: $fmt: file still listed after cpmrm" >&2
        exit 1
    fi
    run fsck.cpm -f "$fmt" -n "$img"
done
echo "smoke test passed"
