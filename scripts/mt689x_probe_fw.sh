#!/bin/bash
# Print the exact firmware file names and sysfs knobs the MT6893 connectivity
# stack uses, so the QEAF firmware manifest lists real names only.
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"

echo "== WIFI_RAM_CODE uses =="
grep -rn 'WIFI_RAM_CODE\|ROM_PATCH\|WIFI_MT\|_MT6893' \
    drivers/misc/mediatek/connectivity/wlan_drv_gen4m 2>/dev/null | head -30

echo "== firmware request sites =="
grep -rn 'request_firmware\|firmware_name\|FW_NAME\|\.bin"' \
    drivers/misc/mediatek/connectivity 2>/dev/null | head -30

echo "== conninfra firmware =="
grep -rn 'request_firmware\|\.bin' drivers/misc/mediatek/connectivity/conninfra 2>/dev/null | head -20
