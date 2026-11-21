#!/bin/bash
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
echo '== firmware_mnt'
debugfs -R 'ls -l /firmware_mnt' "$V" 2>/dev/null | head -20
echo '== firmware_mnt/image'
debugfs -R 'ls -l /firmware_mnt/image' "$V" 2>/dev/null | head -20
echo '== firmware count/size'
debugfs -R 'ls -l /firmware' "$V" 2>/dev/null | awk '/^ +[0-9]+ +[0-9]+\(1\)/{n++; s+=$5} END{print "files",n,"bytes",s}'
