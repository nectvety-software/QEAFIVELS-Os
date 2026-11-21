#!/bin/bash
# Build the static aarch64-musl QRTR host daemons QEAF PID 1 starts before the
# modem: pd-mapper (service registry) and tqftpserv (firmware TFTP). All sources
# are vendored in platform/violet/hosttools/src (upstream projects by
# Andersson/Linaro, see their LICENSE files). No firmware blobs are built in.
set -euo pipefail
ROOT=/mnt/d/QEAFIVELS-Os
HT=$ROOT/platform/violet/hosttools
SRC=$HT/src
OUT=$ROOT/build/violet/hosttools
BUILD=/root/violet-host/build
ZIG=${ZIG:-/mnt/d/QeafivelsOfficial/QeafdropOs/out/toolchain/zig-windows-x86_64-0.13.0/zig.exe}
mkdir -p "$OUT" "$BUILD"

CC="$ZIG cc -target aarch64-linux-musl -static -O2 -g -Wall -Wextra -std=gnu11"
INC="-I$HT/include -I$SRC/qrtr/include"
QRTR_LIB="$SRC/qrtr/lib/qrtr.c $SRC/qrtr/lib/qmi.c $SRC/qrtr/lib/logging.c"

build() {
  local name=$1; shift
  echo "== $name"
  # shellcheck disable=SC2086
  $CC -o "$BUILD/$name" "$@" $INC
  file "$BUILD/$name" 2>/dev/null || true
  cp -f "$BUILD/$name" "$OUT/$name"
}

build pd-mapper \
  "$SRC/pd-mapper/pd-mapper.c" "$SRC/pd-mapper/assoc.c" \
  "$SRC/pd-mapper/json.c" "$SRC/pd-mapper/servreg_loc.c" \
  "$SRC/lzma_stub.c" $QRTR_LIB

build tqftpserv \
  "$SRC/tqftpserv/tqftpserv.c" "$SRC/tqftpserv/translate.c" \
  $QRTR_LIB

echo "built: $(ls "$OUT" | tr '\n' ' ')"
