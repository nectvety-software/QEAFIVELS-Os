# SPDX-License-Identifier: MIT
# Convert a real JPEG/PNG wallpaper into raw RGB565 bytes at the violet UI
# canvas size (720x1560). System.Drawing decodes/resizes; the companion Python
# script packages the bytes into an lv_image_dsc_t C file.
param(
  [string]$SrcImage = "C:\Users\doxuanhop\Downloads\2cb1bea637c46a8d7e21e09d22cd9391.jpg",
  [string]$Output = "D:\QEAFIVELS-Os\build\violet\wallpaper.rgb565",
  [int]$Width = 720,
  [int]$Height = 1560
)

Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile((Resolve-Path $SrcImage).Path)
$bmp = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$gfx.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$gfx.DrawImage($src, 0, 0, $Width, $Height)

$data = $bmp.LockBits((New-Object System.Drawing.Rectangle(0,0,$Width,$Height)),
                      [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                      [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$bmp.UnlockBits($data)

$out = New-Object byte[] ($Width * $Height * 2)
$o = 0
for($y=0; $y -lt $Height; $y++) {
  $row = $y * $data.Stride
  for($x=0; $x -lt $Width; $x++) {
    $i = $row + $x * 4
    $r = [int]$bytes[$i + 2]; $g = [int]$bytes[$i + 1]; $b = [int]$bytes[$i]
    $v = (($r -shr 3) -shl 11) -bor (($g -shr 2) -shl 5) -bor ($b -shr 3)
    $out[$o++] = [byte]($v -band 0xFF)
    $out[$o++] = [byte](($v -shr 8) -band 0xFF)
  }
}
[System.IO.File]::WriteAllBytes($Output, $out)
$gfx.Dispose(); $bmp.Dispose(); $src.Dispose()
Write-Output ("wallpaper: {0}x{1} RGB565 -> {2} ({3} bytes)" -f $Width, $Height, $Output, $out.Length)

