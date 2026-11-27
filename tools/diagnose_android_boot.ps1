$ErrorActionPreference='Continue'
Write-Output '=== DEVICE BOOT IDENTITY ==='
adb shell getprop ro.product.device
adb shell getprop ro.build.fingerprint
adb shell getprop ro.boot.slot_suffix
adb shell cat /proc/cmdline
Write-Output '=== INIT PROCESS ==='
adb shell ps -A | Select-String 'init|qeaf|vqeaf'
Write-Output '=== QEAF MARKERS ==='
adb shell ls -l /init /system/bin/qeaf-init /system/bin/vqeaf-init /system/bin/qeaf-clipboardd 2>&1
Write-Output '=== BOOT LOG MARKERS ==='
adb logcat -d -b all | Select-String -Pattern 'qeaf|QEAF|init:|first_stage_init|vqeaf' | Select-Object -Last 100
Write-Output '=== LOCAL IMAGE ARTIFACTS ==='
Get-ChildItem 'D:\QEAFIVELS-Os\build\violet' -File | Where-Object { $_.Name -match 'boot|ram|cpio|init' } | Select-Object Name,Length,LastWriteTime | Format-Table -AutoSize
