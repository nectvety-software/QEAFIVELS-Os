#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SYSROOT="${SYSROOT:?Set SYSROOT to an ARM64 Linux sysroot with fontconfig, freetype, libinput, libudev, libevdev, libmtdev, and libwacom development files}"
source "${HOME}/.cargo/env"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="${CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER:-aarch64-linux-gnu-gcc}"
export CC_aarch64_unknown_linux_gnu="${CC_aarch64_unknown_linux_gnu:-aarch64-linux-gnu-gcc}"
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$ROOT/target-arm64}"
export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-lmtdev -C link-arg=-levdev -C link-arg=-lwacom -C link-arg=-lpng -C link-arg=-lbz2 -C link-arg=-lbrotlidec -C link-arg=-lz -C link-arg=-lgudev-1.0 -C link-arg=-lgobject-2.0 -C link-arg=-lgio-2.0 -C link-arg=-lglib-2.0 -C link-arg=-lcap -C link-arg=-lffi -C link-arg=-lpcre2-8 -C link-arg=-lbrotlicommon -C link-arg=-Wl,-rpath-link,$SYSROOT/usr/lib/aarch64-linux-gnu"
cargo build --manifest-path "$ROOT/Cargo.toml" --release --target aarch64-unknown-linux-gnu --no-default-features --features violet-kms
