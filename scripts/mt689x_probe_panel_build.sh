#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== panel Makefile entries for leds-lm36273 / ktz8863a / k10a"
grep -n 'lm36273\|ktz8863\|k10a' drivers/gpu/drm/panel/Makefile
echo "== Kconfig entries"
grep -n -B3 -A8 'K10A\|LM36273\|KTZ8863' drivers/gpu/drm/panel/Kconfig
echo "== resolved config"
grep -E 'K10A|LM36273|KTZ8863|MI_DISP' /root/qeaf-mt689x/out/chopin/.config
echo "== built objects?"
ls -l /root/qeaf-mt689x/out/chopin/drivers/gpu/drm/panel/*.o 2>/dev/null | tail -20
