#!/usr/bin/env bash
set -e
S=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot
sha256sum "$S/system/lib64/libc.so" "$S/apex/com.android.runtime/lib64/bionic/libc.so"
readelf -h "$S/system/bin/linker64" | grep -E 'Class:|Machine:|Entry'
readelf -d "$S/system/lib64/libc.so" | grep NEEDED || true
file "$S/system/lib64/libc.so" "$S/system/bin/linker64"
