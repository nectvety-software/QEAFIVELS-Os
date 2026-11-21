#!/bin/bash
# Verify the board DTS really contains the nodes the drivers bind to. A driver
# in the tree is useless if the device tree never instantiates it.
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
DTS=arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts
DTSI=arch/arm64/boot/dts/mediatek/mt6893.dtsi

check() {
  printf '%-28s ' "$1"
  if grep -q "$2" "$DTS" "$DTSI" 2>/dev/null; then echo "present"; else echo "MISSING"; fi
}

check "uart/btif" "mediatek,btif"
check "wifi@18000000" "mediatek,wifi"
check "ufshci" "mediatek,mt6893-ufshci"
check "panel k10a" "panel-k10a"
check "panel l16" "panel-l16"
check "novatek touch" "novatek"
check "scp" "mediatek,scp"
check "scp sensorhub" "scp-sensorhub"
check "afe mt6893 audio" "mediatek,mt6893-audio"
check "cs35l41" "cs35l41"
check "mt6359 codec" "mt6359"
check "usb dwc3" "dwc3"
check "mt6360 typec" "mt6360"
check "lm36273" "lm36273"
check "kvm (arm,virt ok)" "virtualization"

echo "== panel chosen in chopin dts"
grep -n 'panel' $DTS | head -20

echo "== sound node"
grep -n -A12 'sound' $DTS | head -40
