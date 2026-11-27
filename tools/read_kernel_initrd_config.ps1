Write-Output '=== ADB ROOT ==='
adb shell id
adb root
Start-Sleep -Seconds 2
adb shell id
Write-Output '=== KERNEL CONFIG ==='
adb shell "zcat /proc/config.gz 2>/dev/null | grep -E 'CONFIG_BLK_DEV_INITRD|CONFIG_INITRAMFS_SOURCE|CONFIG_RD_GZIP|CONFIG_CMDLINE|CONFIG_CMDLINE_FORCE'"
Write-Output '=== CMDLINE ==='
adb shell cat /proc/cmdline
Write-Output '=== INITRD STATUS ==='
adb shell "ls -l /sys/kernel/debug/boot_params /proc/config.gz 2>/dev/null"
