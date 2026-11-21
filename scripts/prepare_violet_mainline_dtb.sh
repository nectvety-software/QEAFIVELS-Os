#!/bin/bash
set -euo pipefail
SRC=/mnt/d/QEAFIVELS-Os
DTB=$SRC/build/violet/violet-sm6150-abl.dtb
cp "$SRC/../../os-system/NectvetyOS/out/kernel/violet-sm6150.dtb" "$DTB" 2>/dev/null || \
  cp /mnt/d/os-system/NectvetyOS/out/kernel/violet-sm6150.dtb "$DTB"
dtc -I dtb -O dtb -@ -o "$DTB.sym" "$DTB"
mv "$DTB.sym" "$DTB"
fdtput -t x "$DTB" / qcom,msm-id 0x163 0x0
fdtput -t x "$DTB" / qcom,board-id 0x22 0x0
echo "msm-id:   $(fdtget "$DTB" / qcom,msm-id)"
echo "board-id: $(fdtget "$DTB" / qcom,board-id)"
echo "symbols:  $(dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -c __symbols__)"
ls -l "$DTB"
