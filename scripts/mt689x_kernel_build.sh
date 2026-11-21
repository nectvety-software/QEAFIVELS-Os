#!/bin/bash
# QEAF MT6891/MT6893 kernel build.
# Runs inside WSL (Ubuntu-24.04) because the Windows drive holding the repo is
# nearly full.  The kernel tree itself lives in $HOME (see KERNEL_DIR).
#
# Usage: mt689x_kernel_build.sh [defconfig] [jobs]
#   defconfig defaults to chopin (MT6893/MT6891 shared board file).
# Outputs land in $HOME/qeaf-mt689x/out/<defconfig>/.
set -euo pipefail

DEFCONFIG="${1:-chopin}"
JOBS="${2:-$(nproc)}"
SRC="${SRC:-$HOME/qeaf-mt689x/linux}"
OUT="${OUT:-$HOME/qeaf-mt689x/out/$DEFCONFIG}"
FRAGMENT="${FRAGMENT:-/mnt/d/QEAFIVELS-Os/platform/mt689x/kernel/qeaf_mt689x.fragment}"
CROSS="${CROSS:-aarch64-linux-gnu-}"

if [ ! -d "$SRC" ]; then
  echo "kernel tree missing: $SRC" >&2
  exit 1
fi

cd "$SRC"
mkdir -p "$OUT"

# QEAF kernel patches (kept in the repo, applied to the clone). Each one is
# applied only when `git apply --reverse --check` says it is not already
# there, so rerunning the build is safe.
PATCH_DIR="${PATCH_DIR:-/mnt/d/QEAFIVELS-Os/platform/mt689x/kernel/patches}"
if [ -d "$PATCH_DIR" ]; then
  for patch in "$PATCH_DIR"/*.patch; do
    [ -f "$patch" ] || continue
    if git apply --reverse --check "$patch" >/dev/null 2>&1; then
      echo "== patch already applied: $(basename "$patch")"
    elif git apply --check "$patch" >/dev/null 2>&1; then
      git apply "$patch"
      echo "== applied patch: $(basename "$patch")"
    else
      echo "!! patch does not apply: $(basename "$patch")" >&2
      exit 1
    fi
  done
fi


# Vendor defconfig first, then the QEAF fragment on top (merge_config keeps the
# defconfig's own choices and only overrides what the fragment names).
make ARCH=arm64 CROSS_COMPILE="$CROSS" O="$OUT" "${DEFCONFIG}_defconfig"
if [ -f "$FRAGMENT" ]; then
  ./scripts/kconfig/merge_config.sh -O "$OUT" -m "$OUT/.config" "$FRAGMENT"
fi
make ARCH=arm64 CROSS_COMPILE="$CROSS" O="$OUT" olddefconfig

echo "== resolved QEAF symbols =="
for s in MTK_COMBO MTK_COMBO_WIFI MTK_COMBO_BT MTK_BTIF SCSI_UFS_MEDIATEK \
         DRM_MEDIATEK_V2 DRM_PANEL_K10A_36_02_0A TOUCHSCREEN_NOVATEK_NVT_TS \
         SND_SOC_MT6359 MTK_TINYSYS_SCP_SUPPORT MTK_SENSORHUB KVM \
         USB_DWC3 USB_CONFIGFS; do
  printf '%-32s %s\n' "$s" "$(grep -E "^CONFIG_${s}=" "$OUT/.config" || echo '(not set)')"
done

if [ "${CONFIG_ONLY:-0}" = "1" ]; then
  echo "CONFIG_ONLY=1: stopping before the compile"
  exit 0
fi

make ARCH=arm64 CROSS_COMPILE="$CROSS" O="$OUT" -j"$JOBS" Image dtbs modules
rm -rf "$OUT/modules"
make ARCH=arm64 CROSS_COMPILE="$CROSS" O="$OUT" INSTALL_MOD_PATH="$OUT/modules" modules_install

echo "== artifacts =="
ls -l "$OUT/arch/arm64/boot/Image"
ls -l "$OUT/arch/arm64/boot/dts/mediatek/"*.dtb 2>/dev/null || true
echo "== modules: $(find "$OUT/modules" -name '*.ko' | wc -l) files, $(du -sh "$OUT/modules" | cut -f1) =="
