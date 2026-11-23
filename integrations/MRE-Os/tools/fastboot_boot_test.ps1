param(
    [Parameter(Mandatory=$true)][string]$BootImage,
    [string]$Serial = ""
)
$ErrorActionPreference = "Stop"
$Fastboot = Get-Command fastboot -ErrorAction Stop
if (-not (Test-Path $BootImage)) { throw "Boot image not found: $BootImage" }
$devices = @(& $Fastboot.Source devices | Where-Object { $_ -match '\sfastboot$' })
if ($Serial -ne "") { $devices = @($devices | Where-Object { $_ -match "^$([regex]::Escape($Serial))\s" }) }
if ($devices.Count -ne 1) { throw "Expected exactly one matching fastboot device; found $($devices.Count). No command was sent." }
$DeviceSerial = ($devices[0] -split '\s+')[0]
$product = (& $Fastboot.Source -s $DeviceSerial getvar product 2>&1 | Out-String)
if ($product -notmatch '(?i)product:\s*violet') {
    throw "Fastboot product is not confirmed as violet. Refusing to boot."
}
$unlocked = (& $Fastboot.Source -s $DeviceSerial getvar unlocked 2>&1 | Out-String)
if ($unlocked -notmatch '(?i)unlocked:\s*yes') {
    throw "Bootloader is not confirmed unlocked. Refusing to boot a custom image."
}
Write-Host "Temporary RAM boot only. This script contains no flash/erase command."
& $Fastboot.Source -s $DeviceSerial boot (Resolve-Path $BootImage).Path
if ($LASTEXITCODE -ne 0) { throw "fastboot boot failed: $LASTEXITCODE" }
