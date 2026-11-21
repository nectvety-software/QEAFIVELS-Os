#!/bin/bash
# For the devices the QEAF port claims, compare the compatible string in the
# enabled chopin DTS with the of_match strings the drivers actually carry.
# of matching is an exact string compare, so a case difference is a real bug.
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
DTS=arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts
DTSI=arch/arm64/boot/dts/mediatek/mt6893.dtsi

echo "== novatek driver"
find drivers/input/touchscreen -iname '*novatek*' -o -iname '*nvt*' | head
grep -rn 'novatek,NVT-ts-spi' drivers/input/touchscreen | head -5
echo "== novatek dt node"
grep -n -A12 'ts_novatek@0' $DTS | head -20

echo "== typec mt6360 dt"
grep -n -A14 'mt6360_typec' $DTS | head -20
grep -rn 'mediatek,mt6360-tcpc\|mt6360-tcpc' drivers/usb/typec | head -5

echo "== extcon usb"
grep -n -A8 'extcon_usb' $DTS | head -15

echo "== mtu3 dt status"
grep -n -A30 'mtu3_0@11200000' $DTSI | grep -n 'status\|compatible\|extcon' | head

echo "== i2c_lcd_bias node full"
grep -n -A20 'i2c_lcd_bias@11' $DTS | head -25

echo "== sound card dai links target codecs present?"
grep -n -A30 'sound: sound' $DTS | head -45
