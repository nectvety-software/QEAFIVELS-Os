#!/bin/bash
# Extract the complete contents of violet Fastboot-ROM NON-HLOS.bin (FAT) and
# dspso.bin (ext4) into build/ staging. Read-only on the device; ramdisk only.
set -euo pipefail
ROOT=/mnt/d/QEAFIVELS-Os
SRC="$ROOT/build/violet-fw/firmware-update"
OUT="$ROOT/build/firmware-full"
rm -rf "$OUT"
mkdir -p "$OUT/non-hlos" "$OUT/dspso" "$OUT/report"

mdir -i "$SRC/NON-HLOS.bin" -/ -a :: > "$OUT/report/non-hlos-files.txt"
mcopy -i "$SRC/NON-HLOS.bin" -s ::/* "$OUT/non-hlos/" || true

debugfs -R "ls -l /" "$SRC/dspso.bin" 2>/dev/null > "$OUT/report/dspso-root.txt" || true
debugfs -R "rdump / $OUT/dspso" "$SRC/dspso.bin" 2>/dev/null || true

find "$OUT/non-hlos" -type f | wc -l > "$OUT/report/non-hlos-count.txt"
find "$OUT/dspso" -type f | wc -l > "$OUT/report/dspso-count.txt"
du -sb "$OUT" > "$OUT/report/total-bytes.txt"
cat "$OUT/report/non-hlos-count.txt" "$OUT/report/dspso-count.txt" "$OUT/report/total-bytes.txt"
