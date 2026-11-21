#!/bin/bash
# Resolve the module file names the QEAF kernel build will produce, from the
# Makefiles (no guessing from memory). Prints "CONFIG_SYMBOL -> module name".
set -e
cd "${SRC:-$HOME/qeaf-mt689x/linux}"

for mk in \
  drivers/misc/mediatek/connectivity/Makefile \
  drivers/misc/mediatek/connectivity/conninfra/Makefile \
  drivers/misc/mediatek/connectivity/wlan_drv_gen4m/Makefile \
  drivers/misc/mediatek/connectivity/bt/Makefile \
  drivers/misc/mediatek/btif/Makefile \
  drivers/misc/mediatek/sensor/2.0/sensorhub/Makefile \
  sound/soc/mediatek/mt8192/Makefile \
  sound/soc/codecs/Makefile
do
  echo "== $mk"
  grep -nE '^(obj-|.*-objs|.*-y *[,:]?=)' "$mk" 2>/dev/null | head -40
done

echo "== module names from out tree (if any)"
find "${OUT:-$HOME/qeaf-mt689x/out/chopin}" -name '*.ko' 2>/dev/null | sed 's#.*/##' | sort | head -80
