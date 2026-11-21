#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
F=drivers/gpu/drm/panel/panel-k10a-bringup.c
echo "== modes declared"
grep -n 'static const struct drm_display_mode mode_' "$F"
echo "== mode list / preferred"
grep -n -B4 -A20 'supported_modes\|PREFERRED\|num_modes' "$F" | head -60
echo "== which compatible uses which mode list (l16)"
grep -n -B6 -A10 'l16_36_02_0b' "$F" | head -40
echo "== of_match table"
grep -n -A20 'of_device_id.*panel\|panel_of_match\|dt_match' "$F" | head -30
