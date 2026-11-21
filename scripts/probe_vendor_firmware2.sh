#!/bin/bash
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
debugfs -R 'ls -l /firmware' "$V" 2>/dev/null | head -25
echo "== root"
debugfs -R 'ls -l /' "$V" 2>/dev/null | grep -E 'firmware|image|radio' | head -15
