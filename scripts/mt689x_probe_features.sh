#!/bin/bash
# Cross-check the claimed features: does the enabled DTS node exist, does a
# driver carry that exact compatible string, and is its CONFIG on?
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
DTS=arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts
DTSI=arch/arm64/boot/dts/mediatek/mt6893.dtsi
CFG=/root/qeaf-mt689x/out/chopin/.config

have_dt() { grep -q -- "$1" "$DTS" "$DTSI" 2>/dev/null && echo yes || echo NO; }
have_drv() { grep -rqF -- "$1" drivers sound 2>/dev/null && echo yes || echo NO; }

echo "compatible string            dts  driver"
for c in \
  "mediatek,mt6893-ufshci" \
  "mediatek,mt8183-ufshci" \
  "mediatek,mt6885-mtu3" \
  "mediatek,mt6360-tcpc" \
  "mediatek,wifi" \
  "mediatek,btif" \
  "mediatek,scp" \
  "mediatek,mt6893-audio" \
  "mediatek,mt6893-mt6359" \
  "mediatek,i2c_lcd_bias" \
  "mediatek,I2C_LCD_BIAS" \
  "novatek,NVT-ts-spi" \
  "arm,mali-bifrost" \
  "mediatek,mt6893-drm" \
  "mediatek,mt6893-mmsys" \
  "mediatek,dsi" \
  ; do
  printf '%-30s %-4s %s\n' "$c" "$(have_dt "$c")" "$(have_drv "$c")"
done

echo
echo "== dsi0 / display nodes status"
grep -n -A12 'dsi0:' "$DTSI" | grep -E 'compatible|status|clock' | head
echo "== mfg node compatible"
grep -n -B2 -A6 'mali\|mfg' "$DTS" "$DTSI" | grep -E 'compatible|status|@' | head -10
echo "== mt6359 sound node"
grep -rn 'mt6359-sound\|mt6359_sound\|6359.*sound' "$DTS" "$DTSI" | head -5
echo "== key configs"
grep -E '^CONFIG_(DRM_MEDIATEK|DRM_MEDIATEK_V2|DRM_PANEL_K10A_36_02_0A|TOUCHSCREEN_NOVATEK_NVT_TS|MTK_COMBO|MTK_BTIF|MTK_SENSORHUB|SND_SOC_MT6359|PHY_MTK_UFS|USB_MTU3|MFD_MT6360|TYPEC_MT6360)=' "$CFG"
