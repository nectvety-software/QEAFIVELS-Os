#!/bin/bash
set -e
ROOT=/root/violet-host/src
mkdir -p "$ROOT"
cd "$ROOT"
for p in qrtr pd-mapper tqftpserv rmtfs; do
  curl -sSL -o "$p.tar.gz" "https://github.com/andersson/$p/archive/refs/heads/master.tar.gz"
  mkdir -p "$p"
  tar xzf "$p.tar.gz" -C "$p" --strip-components=1
  echo "== $p"
  ls "$p" | head -30
done
