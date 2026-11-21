#!/bin/bash
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
echo '== /lib/modules'
debugfs -R 'ls -l /lib/modules' "$V" 2>/dev/null | head -40
echo '== find wlan.ko'
debugfs -R 'ls -l /lib/modules' "$V" 2>/dev/null | grep -i wlan
