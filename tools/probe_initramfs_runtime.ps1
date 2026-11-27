$ErrorActionPreference='Continue'
Write-Output '=== DEVICE KERNEL CONFIG ==='
adb shell 'zcat /proc/config.gz 2>/dev/null | grep -E "CONFIG_BLK_DEV_INITRD|CONFIG_INITRAMFS_SOURCE|CONFIG_RD_GZIP|CONFIG_CMDLINE|CONFIG_CMDLINE_FORCE"'
Write-Output '=== BOOT COMMAND LINE ==='
adb shell 'cat /proc/cmdline'
Write-Output '=== ROOT MOUNTS ==='
adb shell 'cat /proc/mounts | head -20'
Write-Output '=== QEAF INIT STRINGS ==='
wsl.exe -d Ubuntu-24.04 -- bash -lc "strings /mnt/d/QEAFIVELS-Os/build/violet/vqeaf-init | grep -E 'vqeaf:|boot_stage|PID 1|initramfs|setup_environment' | head -40"
