$ErrorActionPreference='Continue'
Set-Location 'D:\QEAFIVELS-Os'
Write-Output '=== VALIDATE BOTH IMAGE PATHS ==='
py -3 D:\QeafivelsOfficial\QeafdropOs\scripts\validate_bootimg.py --json build\violet\boot-vqeaf-violet-pre.img
py -3 D:\QeafivelsOfficial\QeafdropOs\scripts\validate_bootimg.py --json dist\boot-vqeaf-violet.img
Write-Output '=== HASH / COMPONENT COMPARISON ==='
py -3 -c "import sys,hashlib;sys.path.insert(0,'scripts');import package_violet as p; a=p.read_boot_image('build/violet/boot-vqeaf-violet-pre.img')[1];b=p.read_boot_image('dist/boot-vqeaf-violet.img')[1];print('PRE',hashlib.sha256(open('build/violet/boot-vqeaf-violet-pre.img','rb').read()).hexdigest());print('DIST',hashlib.sha256(open('dist/boot-vqeaf-violet.img','rb').read()).hexdigest());print('kernel_equal',a['kernel']==b['kernel']);print('ramdisk_equal',a['ramdisk']==b['ramdisk']);print('pre init',hashlib.sha256(p.ramdisk_init(a['ramdisk'])).hexdigest());print('dist init',hashlib.sha256(p.ramdisk_init(b['ramdisk'])).hexdigest());print('pre cmdline',repr(a['cmdline']));print('dist cmdline',repr(b['cmdline']))"
