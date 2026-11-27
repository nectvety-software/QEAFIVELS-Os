#!/usr/bin/env bash
set -euo pipefail
ROOT=/mnt/d/QEAFIVELS-Os
SRC=$ROOT/platform/violet/device-tree/sources/guaiyihu-sm6150
BOARD=$ROOT/platform/violet/device-tree/sources/violet-board
BUILD=/mnt/c/QEAF-Violet-DT-Validation
DTS=$BOARD/qeaf-violet-display-overlay.dts
PRE=$BUILD/qeaf-violet-display-overlay.pre.dts
OUT=$BUILD/qeaf-violet-display-overlay.dtbo
mkdir -p "$BUILD"
for f in "$DTS" "$BOARD/xiaomi/violet/violet-sm6150-display.dtsi" \
 "$BOARD/xiaomi/violet/dsi-panel-ft8719-tianma-fhd-video.dtsi" \
 "$BOARD/xiaomi/violet/dsi-panel-nt36672a-shenchao-fhd-video.dtsi" \
 "$SRC/arch/arm64/boot/dts/qcom/sm6150-idp.dtsi"; do
  test -s "$f" || { echo "MISSING_REQUIRED_SOURCE=$f" >&2; exit 20; }
done
cpp -E -P -undef -D__DTS__ -nostdinc -x assembler-with-cpp \
 -I "$SRC/include" -I "$SRC/arch/arm64/boot/dts" \
 -I "$SRC/arch/arm64/boot/dts/qcom" \
 -I "$BOARD" -I "$BOARD/xiaomi/violet" \
 "$DTS" -o "$PRE"
dtc -@ -I dts -O dtb -o "$OUT" "$PRE"
test -s "$OUT"
echo "OUTPUT=$OUT"
sha256sum "$OUT"
dtc -I dtb -O dts "$OUT" > "$BUILD/qeaf-violet-display-overlay.roundtrip.dts"
grep -q 'dsi_nt36672a_shenchao_vid_display' "$BUILD/qeaf-violet-display-overlay.roundtrip.dts"
grep -q 'dsi_ft8719_tianma_vid_display' "$BUILD/qeaf-violet-display-overlay.roundtrip.dts"
grep -q 'qcom,board-id' "$BUILD/qeaf-violet-display-overlay.roundtrip.dts"
echo "DTBO_STATIC_GATE=PASS"
