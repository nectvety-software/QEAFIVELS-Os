#!/usr/bin/env bash
set -euo pipefail
BIN=/tmp/qeaf-slint-android-target/aarch64-linux-android/debug/qeaf-slint-bionic-probe
READELF=/opt/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
echo "=== FILE ==="
file "$BIN"
sha256sum "$BIN"
echo "=== ELF HEADER ==="
"$READELF" -h "$BIN" | grep -E 'Class:|Data:|Type:|Machine:|Entry'
echo "=== INTERPRETER ==="
"$READELF" -l "$BIN" | grep -A1 INTERP
echo "=== DYNAMIC NEEDED / RPATH ==="
"$READELF" -d "$BIN" | grep -E 'NEEDED|RPATH|RUNPATH|SONAME'
echo "=== UNDEFINED DYNAMIC SYMBOLS (first 80) ==="
"$READELF" --dyn-syms --wide "$BIN" | grep ' UND ' | head -80 || true
echo "=== VERSION NEEDS ==="
"$READELF" --version-info "$BIN" | head -80 || true
