#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== mali/kbase drivers in tree"
find drivers -maxdepth 4 -type d \( -iname '*kbase*' -o -iname 'arm' -o -iname 'gpu' \) | head -20
grep -rln 'mediatek,mt6893-mali' drivers 2>/dev/null | head -10
echo "== panfrost of_match"
grep -rn 'mali-bifrost\|mali-valhall\|mali-g77' drivers/gpu/drm/panfrost/*.c | head -10

echo "== mt6359 driver type"
grep -n 'platform_driver\|mfd\|of_device_id\|i2c_driver' sound/soc/codecs/mt6359.c | head -15
echo "== mt6359 of match / name"
grep -n -A8 'of_device_id\|platform_driver mt6359' sound/soc/codecs/mt6359.c | head -30
echo "== pmic mt6359 mfd cells"
grep -rn 'mt6359-sound\|mt6359_sound\|mt6359-accdet\|mt6359_accdet' drivers/mfd drivers/soc 2>/dev/null | head -10
echo "== mt6359 dts node"
grep -rn -B2 -A8 'mt6359' arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts | grep -E 'mt6359|compatible|sound|accdet' | head -20
