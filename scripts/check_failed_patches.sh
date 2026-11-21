#!/bin/bash
K=/root/qeaf-violet-kernel/linux
cd "$K" || exit 1
for n in 0005 0010 0011 0012 0013 0015 0016 0018; do
  p=$(ls /mnt/d/os-system/NectvetyOS/_research/violet-sm6150-mainline/patches/${n}-*.patch)
  echo "===== $(basename "$p")"
  patch -p1 --dry-run --fuzz=3 < "$p" 2>&1 | tail -12
done
