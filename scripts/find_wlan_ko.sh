#!/bin/bash
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
for d in /vendor_dlkm /vendor_dlkm/lib /vendor_dlkm/lib/modules /lib /lib/modules /lib64/modules /lib64; do
  echo "== $d"
  debugfs -R "ls -l $d" "$V" 2>/dev/null | grep -i -E 'wlan|modules|\.ko' | head -20
done
