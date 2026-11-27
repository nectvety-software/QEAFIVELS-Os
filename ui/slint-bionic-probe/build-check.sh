#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$HOME/.cargo/env"
# Keep multi-gigabyte Rust artifacts off the Windows-mounted D: volume.
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-/tmp/qeaf-slint-bionic-core-target}"
export CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-2}"
export CARGO_PROFILE_DEV_DEBUG=0
cd "$ROOT"
cargo check --target aarch64-linux-android
