#!/bin/bash
# Read-only probe of the MT6893 kernel tree: firmware names the connectivity
# stack asks for, and the module/built-in shape of the drivers QEAF needs.
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"

echo "== connectivity dir =="
ls drivers/misc/mediatek/connectivity

echo "== firmware strings in connectivity =="
grep -rhoE '"[A-Za-z0-9_./-]*(MT6893|mt6893|WIFI_|BT_|ROM_|PATCH|EEPROM)[A-Za-z0-9_./-]*"' \
    drivers/misc/mediatek/connectivity 2>/dev/null | sort -u | head -60

echo "== btif driver =="
ls drivers/misc/mediatek/btif

echo "== ufs node in dts =="
grep -n -A12 'ufshci@11270000' arch/arm64/boot/dts/mediatek/mt6893.dtsi | head -30

echo "== wifi node =="
grep -n -A10 'wifi@18000000' arch/arm64/boot/dts/mediatek/mt6893.dtsi | head -20
