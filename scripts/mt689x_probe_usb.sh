#!/bin/bash
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
DTS=arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts
DTSI=arch/arm64/boot/dts/mediatek/mt6893.dtsi

echo "== panel nodes (chopin dts 3420-3500)"
sed -n '3420,3500p' "$DTS"

echo "== usb controller nodes"
grep -n -B3 -A8 'usb@\|usb0\|ssusb\|mtu3\|dwc3' "$DTS" | head -60

echo "== usb in dtsi"
grep -n -A6 'mtu3\|dwc3\|ssusb\|usb@112' "$DTSI" | head -40

echo "== machine driver of_match"
sed -n '1,60p' sound/soc/mediatek/mt8192/mt6893-mt6359.c | grep -n 'compatible\|of_device_id\|MODULE_DEVICE' 
grep -n -A12 'of_device_id' sound/soc/mediatek/mt8192/mt6893-mt6359.c | head -25

echo "== afe of_match"
grep -n -B2 -A8 'mediatek,mt6893-audio' sound/soc/mediatek/mt8192/mt8192-afe-pcm.c

echo "== usb config resolved"
grep -E '^CONFIG_(USB_MTU3|USB_DWC3|USB_XHCI|TYPEC_MT6360|USB_CONFIGFS)=' /root/qeaf-mt689x/out/chopin/.config
