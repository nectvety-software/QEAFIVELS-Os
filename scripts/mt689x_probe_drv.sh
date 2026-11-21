#!/bin/bash
# Which platform driver names does the connectivity stack register? Needed so
# the QEAF Wi-Fi state file can tell "driver bound" from "not built".
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"

echo "== platform_driver instances"
grep -rn 'platform_driver' --include=*.c drivers/misc/mediatek/connectivity | head -20

echo "== .name assignments"
grep -rn '\.name[[:space:]]*=' --include=*.c \
    drivers/misc/mediatek/connectivity/wlan_adaptor \
    drivers/misc/mediatek/connectivity/conninfra 2>/dev/null | head -25

echo "== module names declared"
grep -rn 'MODULE_ALIAS\|MODULE_NAME' --include=*.c \
    drivers/misc/mediatek/connectivity/wlan_adaptor 2>/dev/null | head -10

echo "== dts wifi / bt nodes"
grep -n -A8 'wifi\|btif\|conn' arch/arm64/boot/dts/mediatek/mt6893.dtsi | grep -n 'compatible\|@' | head -20
