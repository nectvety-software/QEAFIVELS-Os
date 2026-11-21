#!/bin/bash
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== MTKBT_BTIF_NAME"
grep -rn 'MTKBT_BTIF_NAME' --include=*.h --include=*.c drivers/misc/mediatek/connectivity | head -5
echo "== wlan init driver registration"
grep -rn 'platform_driver_register' --include=*.c drivers/misc/mediatek/connectivity/wlan_drv_gen4m | head -5
grep -rn '\.name' --include=*.c drivers/misc/mediatek/connectivity/wlan_drv_gen4m/os/linux/gl_init.c | head -10
echo "== adaptor drivers"
grep -rn 'platform_driver' --include=*.c drivers/misc/mediatek/connectivity/wlan_adaptor drivers/misc/mediatek/connectivity/connadp 2>/dev/null | head -10
