#!/usr/bin/env bash
set -euo pipefail
ROOT=/mnt/d/QEAFIVELS-Os
export CARGO_TARGET_DIR=/tmp/qeaf-slint-android-target
export CARGO_BUILD_JOBS=2
export CARGO_PROFILE_DEV_DEBUG=0
export CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$ROOT/tools/aarch64-android24-clang-qeaf"
source /root/.cargo/env
cd "$ROOT/ui/slint-bionic-probe"
cargo check --target aarch64-linux-android --all-targets
cargo build --target aarch64-linux-android --bins
