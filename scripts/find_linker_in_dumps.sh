#!/bin/bash
for img in /mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/{system.img,vendor.img,cust.img}; do
  echo "== $(basename $img)"
  debugfs -R 'ls -l /bin' "$img" 2>/dev/null | grep -i linker | head
  debugfs -R 'ls -l /apex' "$img" 2>/dev/null | head -8
  debugfs -R 'ls -l /lib64' "$img" 2>/dev/null | grep -i linker | head
done
