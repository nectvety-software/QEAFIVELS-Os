#!/bin/bash
set -e
DEST=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
mkdir -p "$DEST/system/bin" "$DEST/system/etc"
cp "$DEST/apex/com.android.runtime/bin/linker64" "$DEST/system/bin/linker64"
for f in libc.so libdl.so libm.so; do
  cp "$DEST/apex/com.android.runtime/lib64/bionic/$f" "$DEST/system/lib64/$f"
done
cp "$DEST/apex/com.android.runtime/lib64/bionic/libdl_android.so" "$DEST/system/lib64/ld-android.so"
cp "$DEST/apex/com.android.runtime/lib64/bionic/libdl_android.so" "$DEST/apex/com.android.runtime/lib64/ld-android.so"
# linker namespace config
for f in ld.config.txt ld.config.vndk_lite.txt; do
  debugfs -R "cat /system/etc/$f" "$S" > "$DEST/system/etc/$f" 2>/dev/null || rm -f "$DEST/system/etc/$f"
done
ls -l "$DEST/system/etc" "$DEST/system/bin"
