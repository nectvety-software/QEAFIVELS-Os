#!/usr/bin/env bash
set -euo pipefail
R=/opt/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
S=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot/system/lib64
B=/tmp/qeaf-slint-android-target/aarch64-linux-android/debug/qeaf-slint-bionic-probe
T=$(mktemp -d)
"$R" --dyn-syms --wide "$B" | awk '$7=="UND" {n=$8; sub(/@.*/,"",n); if(n!="") print n}' | sort -u > "$T/needed"
for lib in libc.so libm.so libdl.so; do
 "$R" --dyn-syms --wide "$S/$lib" | awk '$7!="UND" && $7!="ABS" && $7!="COM" {n=$8; sub(/@.*/,"",n); if(n!="") print n}' >> "$T/exports"
done
sort -u "$T/exports" -o "$T/exports"
echo "Undefined unique symbols: $(wc -l < "$T/needed")"
echo "Missing from staged Bionic libc/libm/libdl:"
comm -23 "$T/needed" "$T/exports"
if comm -23 "$T/needed" "$T/exports" | grep -q .; then rm -rf "$T"; exit 1; fi
echo "PASS: all dynamic undefined symbols resolve by name in staged Bionic libraries"
rm -rf "$T"
