#!/bin/bash
set -e
DEST=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
debugfs -R 'cat /system/etc/linker.config.pb' "$S" > "$DEST/system/etc/linker.config.pb" 2>/dev/null
debugfs -R 'cat /system/apex/com.android.runtime/etc/linker.config.pb' "$S" > "$DEST/apex/com.android.runtime/etc/linker.config.pb" 2>/dev/null || true
mkdir -p "$DEST/apex/com.android.runtime/etc"
debugfs -R 'cat /system/apex/com.android.runtime/etc/linker.config.pb' "$S" > "$DEST/apex/com.android.runtime/etc/linker.config.pb" 2>/dev/null || true
rm -f "$DEST/system/etc/ld.config.txt" "$DEST/system/etc/ld.config.vndk_lite.txt"
find "$DEST" -type f -printf '%P %s\n' | sort | tail -8
