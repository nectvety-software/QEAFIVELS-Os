#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== mt6397-core of_match (mt6359?)"
grep -n -A20 'of_match' drivers/mfd/mt6397-core.c | head -30
echo "== pmic node in chopin dts"
grep -n -B3 -A10 'mt6359' arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts | head -40
echo "== mt6359 compatible anywhere"
grep -rn 'mediatek,mt6359' arch/arm64/boot/dts/mediatek/mt6893* | head -10
echo "== config"
grep -E '^CONFIG_(MFD_MT6397|MFD_MT6359|SND_SOC_MT6359|MTK_PMIC_WRAP)=' /root/qeaf-mt689x/out/chopin/.config
echo "== mali node content"
sed -n '3617,3640p' arch/arm64/boot/dts/mediatek/mt6893.dtsi
