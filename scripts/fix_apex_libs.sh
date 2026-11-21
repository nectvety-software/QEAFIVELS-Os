#!/bin/bash
set -e
D=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot
for f in libdl_android.so libdl.so libc.so libm.so; do
  cp "$D/apex/com.android.runtime/lib64/bionic/$f" "$D/system/lib64/$f"
  chmod 644 "$D/system/lib64/$f"
done
ls -l "$D/system/lib64"/libdl_android.so
