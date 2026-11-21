param(
  [string]$FontPath = (Join-Path $PSScriptRoot "..\assets\fonts\MaterialIcons-Regular.ttf"),
  [string]$CodepointsPath = (Join-Path $PSScriptRoot "..\assets\fonts\MaterialIcons-Regular.codepoints"),
  [string]$OutputPath = (Join-Path $PSScriptRoot "..\system\ui\include\vqeaf\font_icons.h"),
  [string]$PreviewPath = (Join-Path $PSScriptRoot "..\build\icon_atlas_preview.png")
)
# Rasterizes the Material Icons glyphs used by the shell into 40x40 8-bit
# coverage maps. Icons come from Google Fonts (Apache-2.0); see
# assets/fonts/MaterialIcons-LICENSE.txt.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$names = @(
  "info", "settings", "storage", "system_update", "power_settings_new",
  "bolt", "schedule", "lock", "play_arrow", "restart_alt",
  "delete", "home", "arrow_back", "check", "wifi",
  "bluetooth", "visibility", "timer", "smartphone", "file_download",
  "bedtime"
)
$map = @{}
Get-Content $CodepointsPath | ForEach-Object {
  $parts = $_ -split '\s+'
  if($parts.Count -ge 2){ $map[$parts[0]] = [Convert]::ToInt32($parts[1],16) }
}
$cell = 40
$canvas = 64
$fontPx = 48.0
$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile((Resolve-Path $FontPath).Path)
$family = $fonts.Families[0]
$fmt = [System.Drawing.StringFormat]::GenericTypographic
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip

function Get-Alpha([System.Drawing.Bitmap]$bmp, [int]$x, [int]$y) {
  $px = $bmp.GetPixel($x,$y)
  return [Math]::Max($px.R,[Math]::Max($px.G,$px.B))
}

function Render-Glyph([int]$cp) {
  $size = $fontPx
  for($attempt=0;$attempt -lt 4;$attempt++){
    $font = New-Object System.Drawing.Font($family,[single]$size,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
    $bmp = New-Object System.Drawing.Bitmap($canvas,$canvas,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $gfx.Clear([System.Drawing.Color]::Black)
    $gfx.DrawString([string][char]$cp,$font,[System.Drawing.Brushes]::White,[single]0,[single]0,$fmt)
    $gfx.Dispose(); $font.Dispose()
    $minX=$canvas; $minY=$canvas; $maxX=-1; $maxY=-1
    for($y=0;$y -lt $canvas;$y++){
      for($x=0;$x -lt $canvas;$x++){
        if((Get-Alpha $bmp $x $y) -gt 8){
          if($x -lt $minX){$minX=$x}
          if($y -lt $minY){$minY=$y}
          if($x -gt $maxX){$maxX=$x}
          if($y -gt $maxY){$maxY=$y}
        }
      }
    }
    if($maxX -lt 0){ throw ("empty glyph 0x{0:X4}" -f $cp) }
    $bw = $maxX-$minX+1
    $bh = $maxY-$minY+1
    if($bw -le $cell -and $bh -le $cell){
      $alpha = New-Object 'byte[][]' $cell
      for($dy=0;$dy -lt $cell;$dy++){ $alpha[$dy] = New-Object 'byte[]' $cell }
      $ox = [int](($cell-$bw)/2)
      $oy = [int](($cell-$bh)/2)
      for($y=0;$y -lt $bh;$y++){
        for($x=0;$x -lt $bw;$x++){
          $alpha[$oy+$y][$ox+$x] = [byte](Get-Alpha $bmp ($minX+$x) ($minY+$y))
        }
      }
      $bmp.Dispose()
      return ,$alpha
    }
    # The raster overflowed the cell: shrink and re-render once more.
    $size = $size * 0.85
    $bmp.Dispose()
  }
  throw ("glyph 0x{0:X4} does not fit {1}x{1}" -f $cp,$cell)
}

$glyphs = @{}
foreach($name in $names){
  if(-not $map.ContainsKey($name)){ throw "missing codepoint for icon '$name'" }
  $glyphs[$name] = Render-Glyph $map[$name]
}

$lines = New-Object 'System.Collections.Generic.List[string]'
$lines.Add("/* Generated from assets/fonts/MaterialIcons-Regular.ttf (Google Fonts,")
$lines.Add(" * Apache-2.0); do not edit by hand. rows[y][x] is 8-bit coverage. */")
$lines.Add("#ifndef VQEAF_FONT_ICONS_H")
$lines.Add("#define VQEAF_FONT_ICONS_H")
$lines.Add("#include <stdint.h>")
$lines.Add("#define VQEAF_ICON_GLYPH_SIZE 40u")
$lines.Add("typedef struct { uint32_t codepoint;")
$lines.Add("  uint8_t rows[VQEAF_ICON_GLYPH_SIZE][VQEAF_ICON_GLYPH_SIZE]; } VqeafIconGlyph;")
$lines.Add("static VqeafIconGlyph vqeaf_icon_glyphs[] = {")
foreach($name in $names){
  $cp = $map[$name]
  $alpha = $glyphs[$name]
  $body = New-Object 'System.Collections.Generic.List[string]'
  for($y=0;$y -lt $cell;$y++){
    $body.Add("{" + ($alpha[$y] -join ",") + "}")
  }
  $lines.Add(("  /* {0} */" -f $name))
  $lines.Add(("  {{0x{0:X4}u,{{{1}}}}}," -f $cp,($body -join ",")))
}
$lines.Add("};")
$lines.Add("#define VQEAF_ICON_GLYPH_COUNT (sizeof(vqeaf_icon_glyphs)/sizeof(vqeaf_icon_glyphs[0]))")
$lines.Add("#endif")
[System.IO.File]::WriteAllLines([System.IO.Path]::GetFullPath($OutputPath),$lines,[System.Text.Encoding]::ASCII)
Write-Output ("Generated icon atlas: {0} glyphs -> {1}" -f $names.Count,[System.IO.Path]::GetFullPath($OutputPath))

# Preview sheet on the shell background so the rasterization can be eyeballed.
$padding = 16
$cols = 5
$rows = [Math]::Ceiling($names.Count/$cols)
$sheetW = $cols*($cell+$padding)+$padding
$sheetH = $rows*($cell+$padding)+$padding
$sheet = New-Object System.Drawing.Bitmap($sheetW,$sheetH,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$sg = [System.Drawing.Graphics]::FromImage($sheet)
$sg.Clear([System.Drawing.Color]::FromArgb(255,5,11,18))
for($i=0;$i -lt $names.Count;$i++){
  $name = $names[$i]
  $alpha = $glyphs[$name]
  $bx = $padding + ($i % $cols)*($cell+$padding)
  $by = $padding + [Math]::Floor($i/$cols)*($cell+$padding)
  for($y=0;$y -lt $cell;$y++){
    for($x=0;$x -lt $cell;$x++){
      $a = $alpha[$y][$x]
      if($a -eq 0){ continue }
      $r = [int]((234*$a + 5*(255-$a))/255)
      $g = [int]((251*$a + 11*(255-$a))/255)
      $b = [int]((255*$a + 18*(255-$a))/255)
      $sheet.SetPixel($bx+$x,$by+$y,[System.Drawing.Color]::FromArgb(255,$r,$g,$b))
    }
  }
}
$sg.Dispose()
$previewDir = Split-Path -Parent $PreviewPath
if($previewDir -and -not (Test-Path $previewDir)){ New-Item -ItemType Directory -Path $previewDir | Out-Null }
$sheet.Save([System.IO.Path]::GetFullPath($PreviewPath),[System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Output ("Preview -> {0}" -f [System.IO.Path]::GetFullPath($PreviewPath))
