#!/bin/bash
for img in system vendor cust; do
  I=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/$img.img
  echo "===== $img.img"
  for d in /apex /system/apex /vendor/apex /system/system/apex /system_dlkm/apex; do
    out=$(debugfs -R "ls -l $d" "$I" 2>/dev/null | grep -iE 'apex|runtime|\.img|linker' | head -12)
    [ -n "$out" ] && { echo "-- $d"; echo "$out"; }
  done
done
