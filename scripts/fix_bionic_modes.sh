#!/bin/bash
set -e
DEST=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot
find "$DEST" -type f -name '*.so' -exec chmod 644 {} \;
find "$DEST" -type f -name 'linker*' -exec chmod 755 {} \;
find "$DEST" -type f -name 'linker.config.pb' -exec chmod 644 {} \;
find "$DEST" -type f -printf '%m %P\n' | head -12
