#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
F=drivers/gpu/drm/panel/panel-k10a-bringup.c
echo "== bias calls in panel driver"
grep -n 'ktz8863a_\|lm36273_' "$F" | head -40
echo "== ktz8863a driver of_match"
grep -n -A8 'of_device_id' drivers/gpu/drm/panel/leds-ktz8863a.c | head -15
echo "== ktz DT node?"
grep -rn 'ktz8863a\|KTZ8863' arch/arm64/boot/dts/mediatek/ | head -5
echo "== config for bias chips"
grep -E 'KTZ8863|LM36273|LCD_BIAS|BACKLIGHT_LM' /root/qeaf-mt689x/out/chopin/.config
echo "== Kconfig names"
grep -n -B2 -A5 'LM36273\|KTZ8863' drivers/gpu/drm/panel/Kconfig | head -40
