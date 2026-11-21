#!/bin/bash
set -e
DEST=/mnt/d/QEAFIVELS-Os/platform/violet/hosttools
mkdir -p "$DEST/src"
for p in qrtr pd-mapper tqftpserv rmtfs; do
  rm -rf "$DEST/src/$p"
  cp -r "/root/violet-host/src/$p" "$DEST/src/$p"
  rm -f "$DEST/src/$p"/*.tar.gz
done
du -sh "$DEST"/src/*
