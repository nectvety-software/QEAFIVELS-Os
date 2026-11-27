#!/usr/bin/env bash
set -euo pipefail
S=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot/system/lib64
R=/opt/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
for lib in libc.so libm.so libdl.so; do
  echo "== $lib selected symbol exports =="
  "$R" --dyn-syms --wide "$S/$lib" | grep -E ' (memcpy|__errno|fcntl|ioctl|pthread_create|clock_gettime|logf|sinf|getrandom|dlopen|dlsym)(@|$)' | head -40 || true
done
