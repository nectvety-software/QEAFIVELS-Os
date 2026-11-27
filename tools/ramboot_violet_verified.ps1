$ErrorActionPreference = 'Stop'
$img = 'D:\QEAFIVELS-Os\build\violet\boot-vqeaf-violet-pre.img'
$expected = '8917AFE489E16293CE802CE70A628998CE568C7F20279560830E14742A38D369'
if (-not (Test-Path -LiteralPath $img)) { throw 'Boot image missing' }
$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $img).Hash
if ($actual -ne $expected) { throw "SHA256 mismatch: $actual" }
$head = [System.IO.File]::ReadAllBytes($img)[0..7]
if ([System.Text.Encoding]::ASCII.GetString($head) -ne 'ANDROID!') { throw 'Invalid Android boot image magic' }
$devices = @(fastboot devices)
if ($devices.Count -lt 1 -or ($devices -join '') -notmatch 'ca1af25d') { throw 'Expected Violet fastboot device ca1af25d is not present' }
Write-Output "Verified SHA256=$actual"
Write-Output "RAM-booting QEAFIVELS OS image only; no flash/erase/write command will be issued."
fastboot boot $img
if ($LASTEXITCODE -ne 0) { throw "fastboot boot failed with exit code $LASTEXITCODE" }
