param(
  [string]$FontPath = (Join-Path $PSScriptRoot "..\assets\fonts\Ubuntu-Regular.ttf"),
  [string]$OutputPath = (Join-Path $PSScriptRoot "..\system\ui\include\vqeaf\font_ubuntu.h")
)
# Rasterizes Ubuntu-Regular into a 12x16 cell per glyph and stores 8-bit
# coverage so the kernel can alpha-blend text instead of stamping 1-bit masks.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile((Resolve-Path $FontPath).Path)
$family = $fonts.Families[0]
$width = 12
$height = 16
$font = New-Object System.Drawing.Font($family, 12.0, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$fmt = [System.Drawing.StringFormat]::GenericTypographic
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip
$bmp = New-Object System.Drawing.Bitmap($width,$height,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$codepoints = New-Object 'System.Collections.Generic.SortedSet[int]'
for($cp=32;$cp -le 126;$cp++){[void]$codepoints.Add($cp)}
for($cp=0x00C0;$cp -le 0x00FF;$cp++){[void]$codepoints.Add($cp)}
foreach($cp in @(0x0102,0x0103,0x0110,0x0111,0x01A0,0x01A1,0x01AF,0x01B0)){[void]$codepoints.Add($cp)}
for($cp=0x1EA0;$cp -le 0x1EFF;$cp++){[void]$codepoints.Add($cp)}
$lines = New-Object 'System.Collections.Generic.List[string]'
$lines.Add("/* Generated from assets/fonts/Ubuntu-Regular.ttf; do not edit by hand. */")
$lines.Add("/* rows[y][x] is 8-bit coverage (0..255) from the Ubuntu face. */")
$lines.Add("#ifndef VQEAF_FONT_UBUNTU_H")
$lines.Add("#define VQEAF_FONT_UBUNTU_H")
$lines.Add("#include <stdint.h>")
$lines.Add("#define VQEAF_UBUNTU_GLYPH_W 12u")
$lines.Add("#define VQEAF_UBUNTU_GLYPH_H 16u")
$lines.Add("typedef struct { uint32_t codepoint;")
$lines.Add("  uint8_t rows[VQEAF_UBUNTU_GLYPH_H][VQEAF_UBUNTU_GLYPH_W]; } VqeafUbuntuGlyph;")
$lines.Add("static VqeafUbuntuGlyph vqeaf_ubuntu_glyphs[] = {")
foreach($cp in $codepoints){
  $gfx.Clear([System.Drawing.Color]::Black)
  $s = [char]$cp
  $gfx.DrawString([string]$s,$font,[System.Drawing.Brushes]::White,[single]-1,[single]-1,$fmt)
  $rows = New-Object 'System.Collections.Generic.List[string]'
  for($y=0;$y -lt $height;$y++){
    $vals = New-Object 'System.Collections.Generic.List[string]'
    for($x=0;$x -lt $width;$x++){
      $px=$bmp.GetPixel($x,$y)
      $a=[Math]::Max($px.R,[Math]::Max($px.G,$px.B))
      $vals.Add(("{0}" -f $a))
    }
    $rows.Add("{" + ($vals -join ",") + "}")
  }
  $lines.Add(("  {{0x{0:X4}u,{{{1}}}}}," -f $cp,($rows -join ",")))
}
$lines.Add("};")
$lines.Add("#define VQEAF_UBUNTU_GLYPH_COUNT (sizeof(vqeaf_ubuntu_glyphs)/sizeof(vqeaf_ubuntu_glyphs[0]))")
$lines.Add("#endif")
[System.IO.File]::WriteAllLines([System.IO.Path]::GetFullPath($OutputPath),$lines,[System.Text.Encoding]::ASCII)
$gfx.Dispose();$bmp.Dispose();$font.Dispose()
Write-Output ("Generated Ubuntu coverage atlas: {0} glyphs -> {1}" -f $codepoints.Count,[System.IO.Path]::GetFullPath($OutputPath))
