#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== v2 drm of_match strings"
grep -rn 'compatible = "mediatek' drivers/gpu/drm/mediatek/mediatek_v2/*.c | sed 's/.*compatible = //' | sort -u | head -40
echo "== mtk_dsi of_match"
grep -rn -A12 'of_device_id.*dsi\|mtk_dsi_dt_match' drivers/gpu/drm/mediatek/mediatek_v2/mtk_dsi.c | head -25
echo "== dtsi mmsys/drm nodes"
grep -n -A4 'mmsys\|mfg\|disp' arch/arm64/boot/dts/mediatek/mt6893.dtsi | grep -E 'compatible|@' | head -20
echo "== chopin dts disp additions"
grep -n 'disp\|dsi\|mmsys' arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts | head -20
