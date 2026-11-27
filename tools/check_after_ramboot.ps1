Start-Sleep -Seconds 12
Write-Output '=== ADB AFTER RAM BOOT ==='
adb devices -l
Write-Output '=== FASTBOOT AFTER RAM BOOT ==='
fastboot devices
if ((adb devices -l | Out-String) -match 'ca1af25d') {
  Write-Output '=== DEVICE IDENTITY ==='
  adb shell getprop ro.product.device
  adb shell getprop ro.build.fingerprint
  adb shell uname -a
  adb shell getprop ro.boot.bootreason
  Write-Output '=== QEAF PROCESS ==='
  adb shell ps -A | Select-String 'qeaf|vqeaf|init'
  Write-Output '=== QEAF LOG ==='
  adb logcat -d | Select-String -Pattern 'QEAF|VQEAF|PID1|init:' | Select-Object -Last 80
}
