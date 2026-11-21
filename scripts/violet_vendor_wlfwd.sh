#!/bin/bash
set -e
DEST=/mnt/d/QEAFIVELS-Os/platform/violet/hosttools/src
mkdir -p "$DEST/wlfwd"
cp /root/violet-host/src/wlfwd/wlfwd.c /root/violet-host/src/wlfwd/LICENSE "$DEST/wlfwd/"
echo vendored wlfwd
