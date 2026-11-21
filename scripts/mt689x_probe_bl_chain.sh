#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== who calls set_backlight_i2c / setbacklight_control"
grep -rn 'set_backlight_i2c\|setbacklight_control' drivers/gpu/drm/mediatek drivers/gpu/drm/panel/*.h 2>/dev/null | head -20
echo "== brightness call chain in mtk_drm"
grep -rn 'backlight' drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_*.[ch] 2>/dev/null | head -20
