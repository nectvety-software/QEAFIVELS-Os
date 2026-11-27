#!/usr/bin/env bash
set -e
R=/opt/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
B=/tmp/qeaf-slint-android-target/aarch64-linux-android/debug/qeaf-clipboardd
file "$B"
sha256sum "$B"
"$R" -l "$B" | grep -A1 INTERP
"$R" -d "$B" | grep NEEDED
