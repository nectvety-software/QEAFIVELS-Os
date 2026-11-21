#!/bin/bash
set -euo pipefail
K=/root/qeaf-violet-kernel/linux
OUT=/root/qeaf-violet-kernel/out
cd "$K"
cat > "$OUT/wifi-builtin.config" <<'EOF'
CONFIG_QRTR_SMD=y
CONFIG_QRTR_TUN=y
CONFIG_ATH10K_SNOC=y
CONFIG_QCOM_PD_MAPPER=y
EOF
scripts/kconfig/merge_config.sh -O "$OUT" -m "$OUT/.config" "$OUT/wifi-builtin.config"
make ARCH=arm64 LLVM=1 O="$OUT" olddefconfig
grep -E '^CONFIG_(QRTR|QRTR_SMD|ATH10K|ATH10K_SNOC|QCOM_PD_MAPPER|QCOM_QMI_HELPERS|REMOTEPROC|QCOM_RMTFS_MEM)=' "$OUT/.config"
make -j12 ARCH=arm64 LLVM=1 O="$OUT" Image dtbs
ls -l "$OUT/arch/arm64/boot/Image" "$OUT/arch/arm64/boot/dts/qcom/violet-sm6150.dtb"
