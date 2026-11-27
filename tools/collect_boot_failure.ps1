$ErrorActionPreference='Continue'
Write-Output '=== BOOT REASON / KERNEL ==='
adb shell getprop ro.boot.bootreason
adb shell getprop ro.bootmode
adb shell uname -a
adb shell getprop ro.boot.slot_suffix
Write-Output '=== BOOT LOGS ==='
adb logcat -b kernel -d | Select-String -Pattern 'initramfs|/init|No working init|VFS:|Kernel command line|Linux version|Starting init|Run /init|Failed to execute' | Select-Object -Last 120
Write-Output '=== ROOT ACCESS ==='
adb shell su -c 'id; cat /proc/cmdline; dmesg | grep -iE "initramfs|No working init|VFS:|command line|Starting init|Run /init|Failed to execute" | tail -100' 2>&1
