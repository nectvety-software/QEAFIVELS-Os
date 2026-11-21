#!/bin/bash
# Build the research kernel (QRTR + pd_mapper + ath10k) for violet with clang.
set -u
ROOT=/mnt/d/os-system/NectvetyOS/_research/violet-sm6150-mainline
SRC=/root/kbuild/linux-7.2.9
WORK=/root/qeaf-violet-kernel
K=$WORK/linux
OUT=$WORK/out

if [ -d "$K" ]; then
  echo "== refreshing source copy from complete WSL tree"
  rm -rf "$K"
fi
mkdir -p "$WORK"
echo "== copying kernel source to WSL"
cp -a "$SRC" "$K"
cd "$K" || exit 1

echo "== applying research patches"
for p in "$ROOT"/patches/*.patch; do
  if patch -p1 --forward --batch < "$p" >/dev/null 2>&1; then
    echo "OK   $(basename "$p")"
  else
    echo "FAIL $(basename "$p")"
  fi
done

echo "== installing violet DTS"
cp "$ROOT/dt/violet-sm6150.dts" arch/arm64/boot/dts/qcom/ || exit 1

echo "== config"
mkdir -p "$OUT"
cp "$ROOT/configs/config-7.2.10-sm6150-2026-10-27" "$OUT/.config" || exit 1
cat > "$OUT/rmtfs-builtin.config" <<'EOF'
CONFIG_QCOM_RMTFS_MEM=y
EOF
scripts/kconfig/merge_config.sh -O "$OUT" -m "$OUT/.config" \
  "$OUT/rmtfs-builtin.config" || exit 1
make ARCH=arm64 LLVM=1 O="$OUT" olddefconfig || exit 1
echo "== Wi-Fi related config"
for s in QRTR QRTR_SMD QRTR_TUN ATH10K ATH10K_SNOC QCOM_PD_MAPPER QCOM_QMI_HELPERS REMOTEPROC QCOM_RMTFS_MEM; do
  printf '%-22s %s\n' "$s" "$(grep -E "^CONFIG_${s}=" "$OUT/.config" || echo '(not set)')"
done
grep -q '^CONFIG_QCOM_RMTFS_MEM=y$' "$OUT/.config" || {
  echo "ERROR: kernel config did not enable QCOM_RMTFS_MEM" >&2
  exit 1
}

echo "== build Image + dtbs"
make -j12 ARCH=arm64 LLVM=1 O="$OUT" Image dtbs || exit 1

echo "== artifacts"
ls -l "$OUT/arch/arm64/boot/Image"
ls -l "$OUT/arch/arm64/boot/dts/qcom/violet-sm6150.dtb"
