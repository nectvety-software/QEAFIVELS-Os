#!/bin/bash
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
for d in /system/lib/modules /lib/modules /vendor/lib/modules /system/vendor/lib/modules /system_dlkm/lib/modules; do
  echo "== $d"
  debugfs -R "ls -l $d" "$S" 2>/dev/null | grep -i -E 'wlan|\.ko' | head -20
done
