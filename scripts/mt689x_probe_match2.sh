#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
DTS=arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts
DTSI=arch/arm64/boot/dts/mediatek/mt6893.dtsi

echo "== mt6360 mfd cells"
grep -n -A20 'mfd_cell' drivers/mfd/mt6360-core.c | head -40
echo "== mt6360 kconfig/config"
grep -E '^CONFIG_(MFD_MT6360|TYPEC_MT6360|REGULATOR_MT6360|CHARGER_MT6360|MT6360)=' /root/qeaf-mt689x/out/chopin/.config
echo "== mt6360_pmu dt node"
grep -n -B2 -A25 'mt6360_pmu' $DTS | head -45

echo "== mtu3 full node"
sed -n '2997,3030p' $DTSI

echo "== novatek kconfig symbol"
grep -rn -B3 -A6 'NVT36672c\|NOVATEK_NVT_TS' drivers/input/touchscreen/Kconfig drivers/input/touchscreen/Makefile | head -30
