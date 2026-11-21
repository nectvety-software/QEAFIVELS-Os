#!/bin/bash
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
for d in /system/apex/com.android.runtime /system/apex/com.android.runtime/bin /system/apex/com.android.runtime/lib64 /system/apex/com.android.runtime/lib64/bionic; do
  echo "== $d"
  debugfs -R "ls -l $d" "$S" 2>/dev/null | head -25
done
