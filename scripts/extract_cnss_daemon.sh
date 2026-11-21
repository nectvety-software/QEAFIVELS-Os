#!/bin/bash
set -e
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
D=/root/qeaf-bionic
mkdir -p "$D"
echo "== vendor root bin"
debugfs -R 'ls -l /bin' "$V" 2>/dev/null | grep -E 'cnss|wpa_supplicant' | head
echo "== extract cnss-daemon"
debugfs -R 'cat /bin/cnss-daemon' "$V" > "$D/cnss-daemon" 2>/dev/null || \
debugfs -R 'cat /vendor/bin/cnss-daemon' "$V" > "$D/cnss-daemon" 2>/dev/null
ls -l "$D/cnss-daemon"
file "$D/cnss-daemon"
echo "== interp"
readelf -l "$D/cnss-daemon" | grep -A1 'INTERP'
echo "== needed"
readelf -d "$D/cnss-daemon" | grep NEEDED
