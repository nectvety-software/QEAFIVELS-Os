#!/bin/bash
set -e
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
DEST=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot/system/lib64
for f in libvndksupport.so libxml2.so libtinyxml2.so libcutils.so liblog.so libdl.so; do
  if debugfs -R "cat /system/lib64/$f" "$S" > "$DEST/$f" 2>/dev/null && [ -s "$DEST/$f" ]; then
    chmod 644 "$DEST/$f"; echo "got $f"
  else rm -f "$DEST/$f"; echo "MISS $f"; fi
done
