#!/bin/bash
set -e
ROOT=/root/violet-host/src
cd "$ROOT"
for p in wlfwd rmtfs-ro; do :; done
curl -sSL -o wlfwd.tar.gz https://github.com/ScYurii/wlfwd/archive/refs/heads/main.tar.gz
mkdir -p wlfwd
tar xzf wlfwd.tar.gz -C wlfwd --strip-components=1
ls wlfwd
