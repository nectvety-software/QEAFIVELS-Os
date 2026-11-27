$ErrorActionPreference='Stop'
Set-Location 'D:\QEAFIVELS-Os'
$img='D:\QEAFIVELS-Os\dist\boot-vqeaf-violet.img'
$expected='91441cc80c3b0814d277f96c633592ad65ff5ad36de1d962bfcd26624aad7041'
if (-not (Test-Path -LiteralPath $img)) { throw 'Validated dist image missing' }
$actual=(Get-FileHash -Algorithm SHA256 -LiteralPath $img).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "Image hash mismatch: $actual" }
$validation = & py -3 'D:\QeafivelsOfficial\QeafdropOs\scripts\validate_bootimg.py' $img
if ($LASTEXITCODE -ne 0) { throw 'Structural boot image validation failed' }
$devices=@(fastboot devices)
if (($devices -join '') -notmatch 'ca1af25d') { throw 'Violet ca1af25d is not in bootloader fastboot' }
Write-Output "Verified gated image SHA256=$actual"
Write-Output 'Executing fastboot boot only. No flash/erase/update command.'
fastboot boot $img
if ($LASTEXITCODE -ne 0) { throw "fastboot boot failed: $LASTEXITCODE" }
