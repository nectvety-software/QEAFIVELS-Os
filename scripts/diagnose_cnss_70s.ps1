# One command for the CNSS/Wi-Fi capture, end to end:
#   1. flash dist\boot-vqeaf-violet.img to p77 through QEAFRESCUE (the phone
#      writes it itself and verifies byte for byte, then reboots)
#   2. wait until the rebooted QEAFRESCUE answers with a fresh uptime
#   3. request `wifi up` (modem is opt-in) and sample wifi log/dmesg/net/svc
#      every 5 s for 70 s
# The drive letter is detected, never assumed, and is re-detected after the
# reboot. Result: build/logs/wlreset/post-cnss-<stamp>.log
#
#   powershell -NoProfile -File scripts\diagnose_cnss_70s.ps1
#   powershell -NoProfile -File scripts\diagnose_cnss_70s.ps1 -NoFlash   # capture only
param(
    [string]$Image = 'dist\boot-vqeaf-violet.img',
    [switch]$NoFlash,
    [int]$BootTimeout = 420
)
$ErrorActionPreference = 'Continue'
$Root = 'D:\QEAFIVELS-Os'
Set-Location $Root
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogDir = Join-Path $Root 'build\logs\wlreset'
$Log = Join-Path $LogDir ('post-cnss-' + $Stamp + '.log')
$ProbePy = Join-Path $Root 'temp\probe_device.py'
$CheckPy = Join-Path $Root 'scripts\device_check.py'
$ImagePath = if ([System.IO.Path]::IsPathRooted($Image)) { $Image }
             else { Join-Path $Root $Image }
$Volume = ''
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Add-Header([string]$Text) {
    Add-Content -LiteralPath $Log -Value ([Environment]::NewLine + '===== ' + $Text + ' =====')
}
function Add-Block([string]$Text) {
    $Text.TrimEnd() | Add-Content -LiteralPath $Log
}
function Invoke-Probe([string[]]$Commands) {
    try {
        $probeArgs = @('--volume', $script:Volume, '--timeout', '10')
        foreach ($c in $Commands) { $probeArgs += '--cmd'; $probeArgs += $c }
        & python $ProbePy @probeArgs 2>&1 |
            Out-String -Width 4096 | Add-Content -LiteralPath $Log
    } catch {
        ('PROBE ERROR: ' + $_.Exception.Message) | Add-Content -LiteralPath $Log
    }
}
function Find-Volume([int]$Seconds) {
    # Prints the detected letter (or nothing); exit code 2 means "not there".
    $found = (& python $ProbePy --detect-only --timeout $Seconds 2>&1 |
              Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $found) { return '' }
    return $found
}
function Get-Status([string]$Letter) {
    if (-not $Letter) { return '' }
    # The phone writes RESPONSE.TXT while we read it over raw sectors, so a
    # first read can end mid-line; retry until the uptime field is there.
    $text = ''
    for ($i = 0; $i -lt 3; $i++) {
        $text = (& python $ProbePy --volume $Letter --cmd status 2>&1 |
                 Out-String -Width 4096)
        if ($text -match 'uptime=\d+s') { break }
        Start-Sleep -Milliseconds 800
    }
    return $text
}
function Get-Uptime([string]$Text) {
    $m = [regex]::Match($Text, 'uptime=(\d+)s')
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return -1
}

# ---- pre-flight: is the phone there at all? ------------------------------
$pre = Find-Volume 20
if (-not $pre) {
    $msg = 'ABORT: no QEAFRESCUE volume found (no mounted FAT volume with SCREEN.RAW).'
    Write-Host $msg
    Add-Content -LiteralPath $Log -Value $msg
    exit 2
}
$preStatus = Get-Status $pre
$preUptime = Get-Uptime $preStatus
Add-Header ('pre-flight: QEAFRESCUE on ' + $pre +
            ' uptime=' + $preUptime + 's')
Add-Block $preStatus

# ---- 1. flash p77 --------------------------------------------------------
if (-not $NoFlash) {
    if (-not (Test-Path -LiteralPath $ImagePath)) {
        Write-Host ('ABORT: missing image ' + $ImagePath)
        Add-Content -LiteralPath $Log -Value ('ABORT: missing image ' + $ImagePath)
        exit 2
    }
    $hash = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLower()
    $size = (Get-Item -LiteralPath $ImagePath).Length
    $head = 'flash: ' + $ImagePath + ' (' + $size + ' B) sha256=' + $hash
    Write-Host $head
    Add-Header $head
    # device_check stages BOOTIMG.BIN and sends `flash recovery ... confirm`;
    # QEAFRESCUE writes p77, verifies every byte and reboots.
    $flashOut = (& python $CheckPy --image $ImagePath --flash 2>&1 |
                 Out-String -Width 4096)
    $flashCode = $LASTEXITCODE
    Add-Block $flashOut
    Write-Host $flashOut.TrimEnd()
    if ($flashCode -ne 0) {
        $msg = 'ABORT: flash failed (exit ' + $flashCode + ') - see ' + $Log
        Write-Host $msg
        Add-Content -LiteralPath $Log -Value $msg
        exit 1
    }
    Add-Header 'flash returned; waiting for the rebooted QEAFRESCUE'

    # ---- 2. wait for the new boot --------------------------------------
    $deadline = (Get-Date).AddSeconds($BootTimeout)
    $booted = $false
    $sawGone = $false
    while ((Get-Date) -lt $deadline) {
        $letter = Find-Volume 5
        if (-not $letter) {
            if (-not $sawGone) {
                $sawGone = $true
                Add-Content -LiteralPath $Log -Value ('volume dropped at ' +
                    (Get-Date -Format 'HH:mm:ss') + ' (phone rebooting)')
            }
            Start-Sleep -Seconds 2
            continue
        }
        $status = Get-Status $letter
        $up = Get-Uptime $status
        # A fresh boot: the rescue channel answers and uptime went backwards.
        if ($up -ge 0 -and $up -lt 180 -and
            ($preUptime -lt 0 -or $up -lt $preUptime)) {
            $Volume = $letter
            $booted = $true
            Add-Header ('booted: QEAFRESCUE on ' + $letter + ' uptime=' + $up + 's')
            Add-Block $status
            break
        }
        Add-Content -LiteralPath $Log -Value ('waiting: ' + $letter +
            ' uptime=' + $up + 's')
        Start-Sleep -Seconds 2
    }
    if (-not $booted) {
        $msg = 'ABORT: no fresh QEAFRESCUE boot within ' + $BootTimeout +
               ' s - see ' + $Log
        Write-Host $msg
        Add-Content -LiteralPath $Log -Value $msg
        exit 3
    }
} else {
    $Volume = $pre
    Add-Header ('capture only (-NoFlash): QEAFRESCUE on ' + $Volume)
}

# ---- 3. the 70 s capture -------------------------------------------------
Add-Header 'Modem start; QEAF starts native CNSS after modem is online'
Invoke-Probe @('wifi up')
$Clock = [System.Diagnostics.Stopwatch]::StartNew()

$next = 0
while ($Clock.Elapsed.TotalSeconds -le 70) {
    $elapsed = [int][Math]::Floor($Clock.Elapsed.TotalSeconds)
    if ($elapsed -lt $next) {
        Start-Sleep -Milliseconds 250
        continue
    }
    Add-Header ('elapsed=' + $elapsed + 's')
    Invoke-Probe @('wifi log', 'wifi dmesg', 'wifi net', 'wifi svc')
    $next = $elapsed + 5
    Start-Sleep -Seconds 5
}
Add-Header 'END'
'Diagnostic log: ' + $Log
