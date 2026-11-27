#!/usr/bin/env bash
set -e
cd /mnt/d/QEAFIVELS-Os
echo '=== VERIFY VIOLET PACKAGE ==='
python3 scripts/package_violet.py --verify-only
echo '=== IMAGE METADATA ==='
python3 - <<'PY'
import sys, os, gzip, struct, hashlib
sys.path.insert(0,'scripts')
import package_violet as p
path='build/violet/boot-vqeaf-violet-pre.img'
data,h=p.read_boot_image(path)
print({k:v for k,v in h.items() if k not in ('kernel','ramdisk')})
print('image sha256',hashlib.sha256(data).hexdigest())
print('kernel sha256',hashlib.sha256(h['kernel']).hexdigest())
print('ramdisk sha256',hashlib.sha256(h['ramdisk']).hexdigest())
print('ramdisk init sha256',hashlib.sha256(p.ramdisk_init(h['ramdisk'])).hexdigest())
print('built init sha256',hashlib.sha256(open(p.INIT,'rb').read()).hexdigest())
print('init matches',p.ramdisk_init(h['ramdisk'])==open(p.INIT,'rb').read())
print('kernel gzip len',len(gzip.decompress(h['kernel'])))
print('kernel release',p.kernel_identity(h['kernel']))
PY
echo '=== KERNEL CONFIG INITRD ==='
grep -E 'CONFIG_BLK_DEV_INITRD|CONFIG_INITRAMFS_SOURCE|CONFIG_RD_GZIP|CONFIG_DEVTMPFS' build/violet/kernel/.config 2>/dev/null || true
