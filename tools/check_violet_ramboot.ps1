$ErrorActionPreference = 'Continue'
$root = 'D:\QEAFIVELS-Os'
$img = Join-Path $root 'build\violet\boot-vqeaf-violet-pre.img'
Write-Output '=== FASTBOOT DEVICE ==='
fastboot devices
fastboot getvar product 2>&1
fastboot getvar unlocked 2>&1
fastboot getvar is-userspace 2>&1
Write-Output '=== CANDIDATE IMAGE ==='
if (Test-Path -LiteralPath $img) {
  Get-Item -LiteralPath $img | Select-Object FullName,Length,LastWriteTime | Format-List
  Get-FileHash -Algorithm SHA256 -LiteralPath $img | Format-List
  $bytes = [System.IO.File]::ReadAllBytes($img)
  Write-Output ('First 16 bytes: ' + [BitConverter]::ToString($bytes[0..15]))
} else { Write-Output 'No candidate image at expected path' }
Write-Output '=== RAM-BOOT SUPPORT ==='
fastboot help 2>&1 | Select-String -Pattern 'boot <|boot ' | Select-Object -First 5
