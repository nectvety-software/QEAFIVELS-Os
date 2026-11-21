#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== panel drivers mentioning l16 / xiaomi / chopin"
grep -rln 'l16_36_02_0b\|k10a_36_02_0a\|k10a_42_02_0b' drivers/gpu/drm/panel drivers/gpu/drm/mediatek 2>/dev/null | head -10
echo "== compatibles in panel-k10a"
grep -n 'compatible' drivers/gpu/drm/panel/panel-k10a-bringup.c | head -10
echo "== Kconfig panel entries"
grep -n -B2 -A6 'K10A' drivers/gpu/drm/panel/Kconfig | head -30
echo "== l16 files"
find drivers/gpu/drm -iname '*l16*' | head

echo "== machine driver of_match"
grep -n -A12 'of_match\|of_device_id' sound/soc/mediatek/mt8192/mt6893-mt6359.c | head -40

echo "== afe compatible"
grep -n 'mediatek,mt6893-audio\|mt8192_afe_driver\|of_device_id' sound/soc/mediatek/mt8192/mt8192-afe-pcm.c | head -10

echo "== usb config"
grep -E '^CONFIG_(USB_MTU3|USB_MTU3_DUAL_ROLE|USB_DWC3|USB_XHCI_HCD|TYPEC_MT6360|USB_CONFIGFS|USB_GADGET)=' /root/qeaf-mt689x/out/chopin/.config

echo "== mtu3 kconfig"
grep -n -A8 'config USB_MTU3' drivers/usb/mtu3/Kconfig
