#!/bin/bash
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
debugfs -R 'ls -l /firmware/image' "$V" 2>/dev/null | head -30
echo "== count/size"
debugfs -R 'ls -l /firmware/image' "$V" 2>/dev/null | awk '/^ +[0-9]+ +[0-9]+/{n++; s+=$5} END{print n, s}'
