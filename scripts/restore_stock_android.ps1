# SPDX-License-Identifier: MIT
# Put the handset back on its original Android system.
#
# What was changed on the device (see docs/STATUS.md, 2026-11-16):
#   p76/boot    <- QEAFIVELS image          (Android cannot start)
#   p11/vbmeta  <- vbmeta with verity and verification flags cleared
#   p50/misc    <- BCB command "boot-recovery", so every normal boot is
#                  redirected to p77/recovery (where QEAFIVELS lives)
#
# system, vendor, cust, dtbo and userdata were never written, so returning to
# Android needs exactly three writes: the stock boot image, the stock vbmeta
# image and a cleared misc BCB. userdata is never erased by this script.
#
# Stock artifacts (verified against the 2026-10-31 dump of serial ca1af25d,
# out/reference/stock-violet/dump.log):
#   boot.img   sha1 095a4b3fa5f4f942d4461aa5108bd233d0e3982e
#   vbmeta.img sha1 ea1e50699a0e6e734be6cac191ac10442a497da8
#
# Usage (dry run first, then commit with explicit consent):
#   powershell -ExecutionPolicy Bypass -File scripts\restore_stock_android.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\restore_stock_android.ps1 `
#       -Execute -ConfirmPartitionWrite
#
# -Execute alone is not consent: this writes boot, vbmeta and misc, so a second,
# explicit -ConfirmPartitionWrite switch is required. Dry run never writes.
#
# p77/recovery keeps whatever QEAFIVELS image is there. It is not needed to run
# Android (the bootloader only uses it when the BCB asks for recovery); flash a
# stock recovery.img there separately if you want the original recovery too.

param(
    [string]$BootImage = "out\stock-android\boot.img",
    [string]$VbmetaImage = "out\stock-android\vbmeta.img",
    [switch]$SkipVbmeta,
    [switch]$KeepMisc,
    [switch]$Execute,
    [switch]$ConfirmPartitionWrite
)

# fastboot writes normal getvar/progress output to stderr; "Stop" would turn
# those lines into a terminating NativeCommandError. Every real failure is
# checked through $LASTEXITCODE and Abort().
$ErrorActionPreference = "Continue"
$ROOT = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $ROOT "out\logs\restore-stock-$stamp.log"
New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null
$script:log = @()

function Note($msg) {
    $script:log += $msg
    Write-Host $msg
}

function Abort($msg) {
    Note ("ABORT: " + $msg)
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    throw $msg
}

Note "=== violet restore stock Android / $stamp ==="
if ($Execute -and -not $ConfirmPartitionWrite) {
    Note "PARTITION WRITE: fastboot flash boot (p76), flash vbmeta (p11), erase misc (p50)."
    Note "REFUSED: -Execute without -ConfirmPartitionWrite. Nothing was written."
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    exit 2
}
if ($ConfirmPartitionWrite) {
    Note "consent: -ConfirmPartitionWrite given (boot/vbmeta/misc writes are allowed)."
}

# 1. Both stock images must be the exact bytes dumped from this device.
$bootPath = Join-Path $ROOT $BootImage
if (-not (Test-Path $bootPath)) { Abort "boot image not found: $bootPath" }
$bootSha = (Get-FileHash -Algorithm SHA1 -Path $bootPath).Hash.ToLower()
Note ("boot   : " + $bootPath + " (" + (Get-Item $bootPath).Length + " bytes, sha1 " + $bootSha + ")")
if ($bootSha -ne "095a4b3fa5f4f942d4461aa5108bd233d0e3982e") {
    Abort "boot image sha1 is not the stock p76 dump - refusing to flash it"
}

$vbmetaPath = $null
if (-not $SkipVbmeta) {
    $vbmetaPath = Join-Path $ROOT $VbmetaImage
    if (-not (Test-Path $vbmetaPath)) { Abort "vbmeta image not found: $vbmetaPath" }
    $vbmetaSha = (Get-FileHash -Algorithm SHA1 -Path $vbmetaPath).Hash.ToLower()
    Note ("vbmeta : " + $vbmetaPath + " (" + (Get-Item $vbmetaPath).Length + " bytes, sha1 " + $vbmetaSha + ")")
    if ($vbmetaSha -ne "ea1e50699a0e6e734be6cac191ac10442a497da8") {
        Abort "vbmeta image sha1 is not the stock p11 dump - refusing to flash it"
    }
}

# 2. Find fastboot and a single violet device.
$fb = Get-Command fastboot -ErrorAction SilentlyContinue
if (-not $fb) { Abort "fastboot not on PATH. Install platform-tools or add it to PATH." }
$devices = @(& fastboot devices 2>&1 | Where-Object { $_ -match '\S' })
Note ("fastboot devices: " + $(if ($devices.Count -eq 0) { "(none)" } else { $devices -join ' ; ' }))
if ($devices.Count -eq 0) {
    Note "No fastboot device. On violet: power off, hold Volume Down + Power for ~8 s"
    Note "until the fastboot rabbit appears, then replug USB and re-run this script."
    Note "Do NOT flash until the device is visible here."
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    if (-not $Execute) { Write-Host "(dry run) nothing written."; exit 0 }
    Abort "no device in fastboot"
}
if ($devices.Count -ne 1) { Abort "expected exactly one fastboot device, got $($devices.Count)" }

$product = (& fastboot getvar product 2>&1 | Out-String)
Note ("product: " + ($product.Trim() -replace '\s+', ' '))
if ($product -notmatch 'violet') {
    Abort "connected device is not violet - refusing to restore this image"
}

# 3. Plan. userdata, system, vendor, cust, dtbo and p77 are never written.
$plan = @("fastboot flash boot `"$bootPath`"")
if ($vbmetaPath) { $plan += "fastboot flash vbmeta `"$vbmetaPath`"" }
else { Note "SkipVbmeta: p11 stays as it is (verity/verification still off)." }
if ($KeepMisc) { Note "KeepMisc: p50 keeps its BCB, so a normal boot still lands in p77/recovery." }
else { $plan += "fastboot erase misc" }
$plan += "fastboot reboot"
Note "--- planned commands ---"
foreach ($line in $plan) { Note $line }
Note "--- never run: flash recovery (p77 untouched), erase userdata, erase system/vendor ---"
Note "PARTITION WRITE: boot (p76), vbmeta (p11) and misc (p50) are written."
Note "Consent required: -ConfirmPartitionWrite (in addition to -Execute)."

if (-not $Execute) {
    Note "(dry run) nothing written. Re-run with -Execute -ConfirmPartitionWrite once you are sure."
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    exit 0
}

# 4. Execute, one step at a time; stop at the first failure.
Note "--- executing ---"
& fastboot flash boot $bootPath 2>&1 | ForEach-Object { Note $_ }
if ($LASTEXITCODE -ne 0) { Abort "fastboot flash boot failed - the phone is unchanged, re-run" }
Note "flash boot returned OK"

if ($vbmetaPath) {
    & fastboot flash vbmeta $vbmetaPath 2>&1 | ForEach-Object { Note $_ }
    if ($LASTEXITCODE -ne 0) { Note "warn: flash vbmeta failed (boot is already stock; Android still starts)" }
}

if (-not $KeepMisc) {
    & fastboot erase misc 2>&1 | ForEach-Object { Note $_ }
    if ($LASTEXITCODE -ne 0) { Note "warn: erase misc failed (boot is stock, but the BCB may still ask for recovery)" }
}

& fastboot reboot 2>&1 | ForEach-Object { Note $_ }
Note "reboot issued."

Note "--- verification ---"
Note "1. wait ~60 s: the phone should show the Android boot animation, then the launcher."
Note "2. adb devices -> serial ca1af25d."
Note "3. adb shell getprop ro.build.fingerprint ->"
Note "   Xiaomi/violet/violet:13/TQ3A.230901.001.B1/0819:userdebug/release-keys"
Note "4. adb shell getprop ro.boot.verifiedbootstate -> orange (bootloader stays unlocked)."
Note "5. no data was erased: user apps and files are the ones already on the phone."
$script:log | Out-File -FilePath $logPath -Encoding utf8
Write-Host "log: $logPath"
