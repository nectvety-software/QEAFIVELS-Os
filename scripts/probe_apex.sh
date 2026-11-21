#!/bin/bash
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
echo "== /apex"
debugfs -R 'ls -l /apex' "$S" 2>/dev/null | head -30
echo "== stat runtime path"
debugfs -R 'stat /apex/com.android.runtime/bin/linker64' "$S" 2>/dev/null | head -8
