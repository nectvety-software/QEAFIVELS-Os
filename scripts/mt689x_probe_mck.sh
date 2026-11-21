#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== definitions of mt8192_mck_enable/disable"
grep -rn 'mt8192_mck_enable\|mt8192_mck_disable' sound/soc/mediatek/ | head -20
echo "== exports nearby"
grep -rn 'EXPORT_SYMBOL' sound/soc/mediatek/mt8192/*.c | head -40
echo "== Makefile"
cat sound/soc/mediatek/mt8192/Makefile
echo "== config symbols"
grep -E 'SND_SOC_MT8192|SND_SOC_MT6893_MT6359' /root/qeaf-mt689x/out/chopin/.config
