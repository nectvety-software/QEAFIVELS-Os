#!/bin/bash
# Resolve exactly: (a) whether connectivity is built-in or modular, (b) the
# MODULE_NAME used by wlan_drv_gen4m, (c) the .ko names for the audio stack.
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"

echo "== drivers/misc/mediatek/Makefile connectivity gating"
grep -n -B3 -A3 'connectivity' drivers/misc/mediatek/Makefile

echo "== connectivity/Makefile head"
sed -n '1,20p' drivers/misc/mediatek/connectivity/Makefile

echo "== MODULE_NAME definition (wlan_drv_gen4m)"
grep -rn '^MODULE_NAME' drivers/misc/mediatek/connectivity/wlan_drv_gen4m/Makefile

echo "== decision block around 570-600"
sed -n '570,600p' drivers/misc/mediatek/connectivity/wlan_drv_gen4m/Makefile

echo "== conninfra Makefile obj lines"
grep -nE 'obj-|MODULE_NAME|-y *[:+]?=' drivers/misc/mediatek/connectivity/conninfra/Makefile | head -20

echo "== bt Makefiles"
find drivers/misc/mediatek/connectivity/bt -name Makefile | head
for m in $(find drivers/misc/mediatek/connectivity/bt -name Makefile | head -6); do
  echo "-- $m"; grep -nE 'obj-|\-y *[:+]?=|MODULE_NAME' "$m" | head -10
done

echo "== mt6359 / accdet / cs35l41 codec module names"
grep -nE 'snd-soc-mt6359|mt6359-accdet|snd-soc-cs35l41' sound/soc/codecs/Makefile

echo "== mt8192 machine makefile"
cat sound/soc/mediatek/mt8192/Makefile
