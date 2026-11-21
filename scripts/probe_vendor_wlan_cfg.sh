#!/bin/bash
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
echo '== /firmware/wlan'
debugfs -R 'ls -l /firmware/wlan' "$V" 2>/dev/null | head -30
echo '== /firmware/wlan/qca_cld'
debugfs -R 'ls -l /firmware/wlan/qca_cld' "$V" 2>/dev/null | head -30
echo '== stat wlan_mac.bin'
debugfs -R 'stat /firmware/wlan/qca_cld/wlan_mac.bin' "$V" 2>/dev/null | head -12
