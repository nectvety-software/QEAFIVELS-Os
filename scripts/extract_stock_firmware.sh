#!/bin/bash
# Extract the device's own firmware from the stock vendor dump into ramdisk staging.
set -u
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
DEST=/mnt/d/QEAFIVELS-Os/build/violet/firmware-stock
mkdir -p "$DEST"
debugfs -R 'ls -l /firmware' "$V" 2>/dev/null |
  awk '{for(i=1;i<=NF;i++) if($i=="(1)") print $NF}' |
  while read -r name; do
    [ -n "$name" ] || continue
    debugfs -R "cat /firmware/$name" "$V" > "$DEST/$name" 2>/dev/null
  done
echo "files: $(find "$DEST" -type f | wc -l), bytes: $(du -sb "$DEST" | cut -f1)"
ls "$DEST" | grep -E 'adsp.mdt|modem.mdt|wlanmdsp|cdsp.mdt|ipa_fws|bdwlan|regdb' | head -20
