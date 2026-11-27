#!/usr/bin/env bash
cd /mnt/d/QEAFIVELS-Os
echo '=== IMAGE BUILD REFERENCES ==='
grep -RInE 'boot-vqeaf-violet-pre|qeaf-initramfs|mkbootimg|vendor_boot|ramdisk' tools scripts build docs --include='*.sh' --include='*.py' --include='*.ps1' --include='*.md' 2>/dev/null | head -220
echo '=== BOOT TOOLS ==='
command -v unpack_bootimg || true
command -v mkbootimg || true
find . -maxdepth 5 -type f \( -name mkbootimg.py -o -name unpack_bootimg.py -o -name mkbootimg -o -name unpack_bootimg \) -print | head -50
echo '=== INIT FILES ==='
find system -maxdepth 4 -type f 2>/dev/null | head -100
