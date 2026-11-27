$ErrorActionPreference = 'Stop'
$root = 'D:\QEAFIVELS-Os'
$base = Join-Path $root 'dist\boot-vqeaf-violet-diagnostic-20261126.img'
$kernel = Join-Path $root 'build\kernel-4.14.117-experimental\arch\arm64\boot\Image.gz'
$config = Join-Path $root 'build\kernel-4.14.117-experimental\.config'
$releaseHeader = Join-Path $root 'build\kernel-4.14.117-experimental\include\generated\utsrelease.h'
$dtb = Join-Path $root 'build\violet-device-tree-micode-q\violet-sm6150-merged.dtb'
$out = Join-Path $root 'dist\boot-vqeaf-violet-exp-4.14.117-ramboot.img'
$manifest = Join-Path $root 'dist\boot-vqeaf-violet-exp-4.14.117-ramboot.manifest.json'
$expectedBase = '1724047675E3E193ED95EFBA5A7997D516A79D5E6EC4BFA513A8F65399D33A65'
$expectedRamdisk = '0BE1B8277F3BA7A9952C6DB44C1F452E8E06798634D07CAA069097342872648C'
$expectedDtb = '6539CE8CAF68575CF3266800525FD38C10180443FFFC47AE549866095F85E324'
if (!(Test-Path $base) -or !(Test-Path $kernel) -or !(Test-Path $config) -or
    !(Test-Path $releaseHeader) -or !(Test-Path $dtb)) {
    throw 'Missing base image, Image.gz, kernel config/release, or verified merged Violet DTB.'
}
if ((Get-FileHash -Algorithm SHA256 $base).Hash -ne $expectedBase) { throw 'Base diagnostic image SHA-256 mismatch.' }
if ((Get-FileHash -Algorithm SHA256 $dtb).Hash -ne $expectedDtb) { throw 'Merged Violet DTB SHA-256 mismatch.' }
$baseBytes = [IO.File]::ReadAllBytes($base)
if ([Text.Encoding]::ASCII.GetString($baseBytes,0,8) -ne 'ANDROID!') { throw 'Invalid Android boot image magic.' }
$oldKernelSize = [BitConverter]::ToUInt32($baseBytes,8)
$ramdiskSize = [BitConverter]::ToUInt32($baseBytes,16)
$kernelAddr = [BitConverter]::ToUInt32($baseBytes,12)
$ramdiskAddr = [BitConverter]::ToUInt32($baseBytes,20)
$pageSize = [BitConverter]::ToUInt32($baseBytes,36)
$headerVersion = [BitConverter]::ToUInt32($baseBytes,40)
$recoverySize = [BitConverter]::ToUInt32($baseBytes,1632)
if ($kernelAddr -ne 0x00008000 -or $ramdiskAddr -ne 0x01000000) {
    throw "Unexpected load addresses (kernel=$('0x{0:X8}' -f $kernelAddr) ramdisk=$('0x{0:X8}' -f $ramdiskAddr))."
}
if ($pageSize -ne 4096 -or $headerVersion -ne 1 -or $recoverySize -ne 0) { throw "Unexpected boot header (page=$pageSize version=$headerVersion recovery_dtbo=$recoverySize)." }
$ramdiskOffset = $pageSize + [int]([Math]::Ceiling($oldKernelSize / [double]$pageSize) * $pageSize)
if (($ramdiskOffset + $ramdiskSize) -gt $baseBytes.Length) { throw 'Ramdisk range is outside base image.' }
$ramdisk = New-Object byte[] $ramdiskSize
[Array]::Copy($baseBytes,$ramdiskOffset,$ramdisk,0,$ramdiskSize)
$sha = [Security.Cryptography.SHA256Managed]::new()
$ramdiskHash = ([BitConverter]::ToString($sha.ComputeHash($ramdisk))).Replace('-','')
$sha.Dispose()
if ($ramdiskHash -ne $expectedRamdisk) { throw 'Diagnostic ramdisk SHA-256 mismatch.' }
$releaseText = [IO.File]::ReadAllText($releaseHeader)
$releaseMatch = [regex]::Match($releaseText, '#define UTS_RELEASE "([^"]+)"')
if (!$releaseMatch.Success -or $releaseMatch.Groups[1].Value -notlike '4.14.117*') {
    throw 'Experimental kernel release header is missing or does not identify 4.14.117.'
}
$kernelRelease = $releaseMatch.Groups[1].Value
$configLines = [IO.File]::ReadAllLines($config)
$requiredConfig = @(
    'CONFIG_ARM64=y',
    'CONFIG_BUILD_ARM64_KERNEL_COMPRESSION_GZIP=y',
    'CONFIG_BLK_DEV_INITRD=y',
    'CONFIG_INITRAMFS_SOURCE=""',
    'CONFIG_CMDLINE_EXTEND=y',
    'CONFIG_DRM_MSM=y',
    '# CONFIG_DRM_MSM_DSI is not set',
    'CONFIG_DRM_MSM_DSI_STAGING=y',
    '# CONFIG_DRM_FBDEV_EMULATION is not set',
    'CONFIG_USB_DWC3_DUAL_ROLE=y',
    'CONFIG_USB_DWC3_MSM=y',
    'CONFIG_USB_GADGET=y',
    'CONFIG_USB_CONFIGFS_MASS_STORAGE=y'
)
foreach ($required in $requiredConfig) {
    if ($configLines -notcontains $required) { throw "Required kernel config is missing: $required" }
}
$configSha = (Get-FileHash -Algorithm SHA256 $config).Hash
$kernelBytes = [IO.File]::ReadAllBytes($kernel)
$dtbBytes = [IO.File]::ReadAllBytes($dtb)
if ($kernelBytes.Length -lt 2 -or $kernelBytes[0] -ne 0x1f -or $kernelBytes[1] -ne 0x8b) { throw 'Image.gz is not gzip-compressed.' }
if ($dtbBytes.Length -lt 4 -or $dtbBytes[0] -ne 0xd0 -or $dtbBytes[1] -ne 0x0d -or $dtbBytes[2] -ne 0xfe -or $dtbBytes[3] -ne 0xed) { throw 'Merged DTB does not start with FDT magic.' }
$dtbText = [Text.Encoding]::ASCII.GetString($dtbBytes)
if (!$dtbText.Contains('qcom,mdss_dsi_ft8719_tianma_fhd_video') -or !$dtbText.Contains('qcom,mdss_dsi_nt36672a_shenchao_fhd_video')) { throw 'Both Tianma and Shenchao panel descriptors must be present.' }
$kernelPayload = New-Object byte[] ($kernelBytes.Length + $dtbBytes.Length)
[Array]::Copy($kernelBytes,0,$kernelPayload,0,$kernelBytes.Length)
[Array]::Copy($dtbBytes,0,$kernelPayload,$kernelBytes.Length,$dtbBytes.Length)
$header = New-Object byte[] $pageSize
[Array]::Copy($baseBytes,0,$header,0,$pageSize)
[BitConverter]::GetBytes([uint32]$kernelPayload.Length).CopyTo($header,8)
[BitConverter]::GetBytes([uint32]$ramdisk.Length).CopyTo($header,16)
# Recompute the legacy boot image ID over kernel, ramdisk and their size words.
$idStream = New-Object IO.MemoryStream
$idStream.Write($kernelPayload,0,$kernelPayload.Length)
$idStream.Write($ramdisk,0,$ramdisk.Length)
$bw = New-Object IO.BinaryWriter($idStream)
$bw.Write([uint32]$kernelPayload.Length); $bw.Write([uint32]$ramdisk.Length); $bw.Write([uint32]0); $bw.Write([uint32]0)
$sha1 = [Security.Cryptography.SHA1Managed]::new()
$id = $sha1.ComputeHash($idStream.ToArray())
[Array]::Clear($header,576,32)
[Array]::Copy($id,0,$header,576,$id.Length)
$sha1.Dispose(); $bw.Dispose(); $idStream.Dispose()
$stream = New-Object IO.MemoryStream
$stream.Write($header,0,$header.Length)
$stream.Write($kernelPayload,0,$kernelPayload.Length)
$pad = ( $pageSize - ($stream.Length % $pageSize) ) % $pageSize
if ($pad -gt 0) { $stream.Write((New-Object byte[] $pad),0,$pad) }
$stream.Write($ramdisk,0,$ramdisk.Length)
$pad = ( $pageSize - ($stream.Length % $pageSize) ) % $pageSize
if ($pad -gt 0) { $stream.Write((New-Object byte[] $pad),0,$pad) }
if ($stream.Length -gt 64MB) {
    $size = $stream.Length
    $stream.Dispose()
    throw "Boot image is $size bytes, exceeding the 64 MiB limit."
}
[IO.File]::WriteAllBytes($out,$stream.ToArray())
$stream.Dispose()
$shaOut = (Get-FileHash -Algorithm SHA256 $out).Hash
$shaKernel = (Get-FileHash -Algorithm SHA256 $kernel).Hash
$shaDtb = (Get-FileHash -Algorithm SHA256 $dtb).Hash
$info = [ordered]@{
  target_device='violet'
  purpose='experimental RAM boot only'
  source_kernel='MiCode 4.14.117 experimental worktree; not claimed compatible with 4.14.215'
  kernel_release=$kernelRelease
  kernel_config=$config
  kernel_config_sha256=$configSha
  kernel_image=$kernel
  kernel_sha256=$shaKernel
  appended_merged_dtb=$dtb
  dtb_sha256=$shaDtb
  ramdisk_sha256=$ramdiskHash
  base_image_sha256=$expectedBase
  boot_header_version=$headerVersion
  page_size=$pageSize
  kernel_load_address=$('0x{0:X8}' -f $kernelAddr)
  ramdisk_load_address=$('0x{0:X8}' -f $ramdiskAddr)
  panel_descriptors=@('ft8719-tianma-fhd-video','nt36672a-shenchao-fhd-video')
  image_size_limit=67108864
  output_image=$out
  output_size=(Get-Item -LiteralPath $out).Length
  output_sha256=$shaOut
  partition_writes=0
  production_ready=$false
  physical_boot_tested=$false
}
$info | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $manifest
Write-Output "CREATED=$out"
Write-Output "SIZE=$((Get-Item $out).Length)"
Write-Output "SHA256=$shaOut"
Write-Output "MANIFEST=$manifest"
