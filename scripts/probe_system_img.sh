#!/bin/bash
IMG=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
debugfs -R 'stat /system/bin/linker64' "$IMG" 2>/dev/null | head -14
echo "== libs"
debugfs -R 'ls -l /system/lib64' "$IMG" 2>/dev/null | grep -E 'libc\.so|libdl\.so|liblog\.so|libcutils\.so|libbinder\.so|libhwbinder\.so|libnetutils' | head -12
