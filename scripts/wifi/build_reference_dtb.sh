#!/bin/bash
set -euo pipefail
SRC=/mnt/d/QEAFIVELS-Os/platform/violet/device-tree/sources/guaiyihu-sm6150
BUILD=/mnt/d/QEAFIVELS-Os/platform/violet/device-tree/build
OUT=$BUILD/violet-reference.dtb
mkdir -p "$BUILD"
cpp -E -P -undef -D__DTS__ -nostdinc -x assembler-with-cpp \
  -I "$SRC/include" \
  -I "$SRC/arch/arm64/boot/dts" \
  -I "$SRC/arch/arm64/boot/dts/qcom" \
  "$SRC/arch/arm64/boot/dts/qcom/sm6150.dts" -o "$BUILD/sm6150.pre.dts"
dtc -I dts -O dtb -@ -q -i "$SRC/include" -o "$OUT" "$BUILD/sm6150.pre.dts"
ls -l "$OUT"
