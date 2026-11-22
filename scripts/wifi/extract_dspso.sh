#!/bin/bash
set -u
SRC=/mnt/d/QEAFIVELS-Os/build/violet-fw/firmware-update/dspso.bin
OUT=/mnt/d/QEAFIVELS-Os/build/firmware-full/dspso
mkdir -p "$OUT"
debugfs -R "ls -l /" "$SRC" 2>/dev/null | head -40
echo "--- extracting ---"
debugfs -R "rdump / $OUT" "$SRC" 2>&1 | tail -5
echo "--- count ---"
find "$OUT" -type f | wc -l
du -sb "$OUT"
