# SPDX-License-Identifier: MIT
# Rasterise a small emoji repertoire from Segoe UI Emoji (Windows) into an
# LVGL 9 fmt_txt font so QEAF's soft keyboard can show REAL emoji glyphs.
# Emoji codepoints are > BMP, so DrawString uses ConvertFromUtf32 and cmap
# range_start is 32-bit while unicode_list offsets stay 16-bit.
param([string]$Font = "C:\Windows\Fonts\seguiemj.ttf",
      [string]$Out = "D:\QEAFIVELS-Os\platform\violet\ui\fonts\vqeaf_emoji_28.c")

Add-Type -AssemblyName System.Drawing

# Common emoji, grouped faces / gestures / hearts / nature / food / objects.
$emoji = @(
  0x1F600,0x1F603,0x1F604,0x1F601,0x1F606,0x1F605,0x1F602,0x1F923,0x1F60A,0x1F607,
  0x1F642,0x1F643,0x1F609,0x1F60D,0x1F618,0x1F61A,0x1F61C,0x1F62D,0x1F631,0x1F620,
  0x1F621,0x1F60E,0x1F913,0x1F60F,0x1F634,0x1F635,0x1F610,0x1F611,0x1F616,0x1F636,0x1F60B,
  0x1F61B,0x1F628,0x1F629,0x1F62B,0x1F62C,0x1F62D,0x1F62E,0x1F62F,0x1F631,0x1F620,0x1F65B,
  0x1F914,0x1F928,0x1F929,0x1F92A,0x1F92D,0x1F92B,0x1F92C,0x1F92F,
  0x1F44D,0x1F44E,0x1F44A,0x1F44B,0x1F44C,0x1F44F,0x1F64C,0x1F64F,0x1F90C,0x1F91D,
  0x2764,0x1F49B,0x1F49A,0x1F499,0x1F49C,0x1F5A9,0x1F494,0x1F495,0x1F49E,0x1F493,
  0x1F31F,0x1F4A5,0x1F525,0x1F389,0x1F38A,0x1F388,0x1F3B0,0x1F3B2,0x1F680,0x1F681,
  0x1F642,0x1F600,0x1F601,0x1F60A,0x1F60D,0x1F602,0x1F609,0x1F618,0x1F61D,0x1F60C,
  0x1F34E,0x1F34A,0x1F353,0x1F345,0x1F355,0x1F363,0x1F369,0x1F370,0x1F37A,0x1F963,
  0x1F4A9,0x1F4A6,0x1F4A3,0x1F4AA,0x1F4AF,0x1F44D,0x1F44E,0x1F44F,0x1F64F,0x2705
) | Sort-Object -Unique

$size = [single]28
$collection = New-Object System.Drawing.Text.PrivateFontCollection
$collection.AddFontFile((Resolve-Path $Font).Path)
$family = $collection.Families[0]
$emojiFont = New-Object System.Drawing.Font($family, $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$em = [single]$family.GetEmHeight([System.Drawing.FontStyle]::Regular)
$ascent = [int][Math]::Ceiling($family.GetCellAscent([System.Drawing.FontStyle]::Regular) * $size / $em)
$descent = [int][Math]::Ceiling($family.GetCellDescent([System.Drawing.FontStyle]::Regular) * $size / $em)
$pad = [int][Math]::Ceiling($size * 0.35) + 1
$width = [int][Math]::Ceiling($size * 2.2) + $pad * 2
$height = [int][Math]::Ceiling($size * 1.8) + $pad * 2
$bmp = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$fmt = [System.Drawing.StringFormat]::GenericTypographic.Clone()
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces
$brush = [System.Drawing.Brushes]::White
$rect = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
$lineHeight = $ascent + $descent + 2
$baseLine = $descent + 2

$bitmapBytes = New-Object 'System.Collections.Generic.List[byte]'
$descs = New-Object 'System.Collections.Generic.List[string]'
$descs.Add("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}, /* id = 0 reserved */")
$unicode = New-Object 'System.Collections.Generic.List[int]'
$minCp = 0x10FFFF; $maxCp = 0

foreach($cp in $emoji) {
  $gfx.Clear([System.Drawing.Color]::Black)
  $s = [char]::ConvertFromUtf32($cp)
  $pt = New-Object System.Drawing.PointF([single]$pad, [single]$pad)
  $gfx.DrawString($s, $emojiFont, $brush, $pt, $fmt)
  $advance = $gfx.MeasureString($s, $emojiFont, $pt, $fmt).Width
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $data.Stride
  $bytes = New-Object byte[] ($stride * $height)
  [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
  $bmp.UnlockBits($data)
  $left = $width; $right = -1; $top = $height; $bottom = -1
  for($y = 0; $y -lt $height; $y++) {
    $row = $y * $stride
    for($x = 0; $x -lt $width; $x++) {
      if($bytes[$row + $x * 4 + 2] -gt 0) {
        if($x -lt $left){$left=$x}; if($x -gt $right){$right=$x}
        if($y -lt $top){$top=$y}; if($y -gt $bottom){$bottom=$y}
      }
    }
  }
  $boxW=0; $boxH=0; $ofsX=0; $ofsY=0
  if($right -ge 0) {
    $boxW = $right - $left + 1; $boxH = $bottom - $top + 1
    $ofsX = $left - $pad; $ofsY = ($pad + $ascent) - $bottom - 1
    for($y = 0; $y -lt $boxH; $y++) {
      $row = ($top + $y) * $stride
      for($x = 0; $x -lt $boxW; $x++) { $bitmapBytes.Add($bytes[$row + ($left + $x) * 4 + 2]) }
    }
  }
  $advW = [int][Math]::Round($advance * 16.0)
  $bitmapIndex = $bitmapBytes.Count - ($boxW * $boxH)
  $descs.Add(("    {{.bitmap_index = {0}, .adv_w = {1}, .box_w = {2}, .box_h = {3}, .ofs_x = {4}, .ofs_y = {5}}}, /* U+{6:X} */" -f $bitmapIndex, $advW, $boxW, $boxH, $ofsX, $ofsY, $cp))
  [void]$unicode.Add($cp)
  if($cp -lt $minCp){$minCp=$cp}; if($cp -gt $maxCp){$maxCp=$cp}
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("/* Generated from Segoe UI Emoji at ${size}px by scripts/generate_emoji_font.ps1; do not edit. */")
[void]$sb.AppendLine("#include `"lvgl.h`"")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#ifndef LV_ATTRIBUTE_LARGE_CONST")
[void]$sb.AppendLine("#define LV_ATTRIBUTE_LARGE_CONST")
[void]$sb.AppendLine("#endif")
[void]$sb.AppendLine("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
for($i=0; $i -lt $bitmapBytes.Count; $i+=16) {
  $n=[Math]::Min(16,$bitmapBytes.Count-$i); $c=New-Object string[] $n
  for($k=0;$k -lt $n;$k++){$c[$k]=""+$bitmapBytes[$i+$k]}
  [void]$sb.AppendLine("    " + [string]::Join(",",$c) + ",")
}
[void]$sb.AppendLine("};")
# Group emoji by 0x10000 block so each sparse cmap's uint16 unicode_list
# offsets stay < 65536 (LVGL limit). One cmap + list per block.
$blocks = @{}
for($gi=0; $gi -lt $unicode.Count; $gi++) {
  $base = $unicode[$gi] -band 0xFFFF0000
  if(-not $blocks.ContainsKey($base)) { $blocks[$base] = New-Object System.Collections.Generic.List[int] }
  $blocks[$base].Add($gi)
}
$blkIdx = 0
foreach($base in ($blocks.Keys | Sort-Object)) {
  $idxs = $blocks[$base]
  [void]$sb.AppendLine("static const uint16_t unicode_list_$blkIdx" + "[] = {")
  for($i=0; $i -lt $idxs.Count; $i+=12) {
    $n=[Math]::Min(12,$idxs.Count-$i); $c=New-Object string[] $n
    for($k=0;$k -lt $n;$k++){$c[$k]="0x{0:X}" -f ($unicode[$idxs[$i+$k]] - $base)}
    [void]$sb.AppendLine("    " + [string]::Join(",",$c) + ",")
  }
  [void]$sb.AppendLine("};")
  $blkIdx++
}
[void]$sb.AppendLine("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
foreach($d in $descs){[void]$sb.AppendLine($d)}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
$blkIdx = 0
foreach($base in ($blocks.Keys | Sort-Object)) {
  $idxs = $blocks[$base]
  $bmax = $unicode[$idxs[$idxs.Count-1]]
  [void]$sb.AppendLine(("    {{.range_start = {0}, .range_length = {1}, .glyph_id_start = {2}," -f $base, ($bmax-$base+1), ($idxs[0]+1)))
  [void]$sb.AppendLine(("     .unicode_list = unicode_list_$blkIdx, .glyph_id_ofs_list = NULL," -f ""))
  [void]$sb.AppendLine(("     .list_length = {0}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}," -f $idxs.Count))
  $blkIdx++
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("static const lv_font_fmt_txt_dsc_t font_dsc = {")
[void]$sb.AppendLine("    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,")
[void]$sb.AppendLine("    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = $blkIdx, .bpp = 8,")
[void]$sb.AppendLine("    .kern_classes = 0, .bitmap_format = LV_FONT_FMT_TXT_PLAIN, .stride = 0,")
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("const lv_font_t vqeaf_emoji_28 = {")
[void]$sb.AppendLine("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
[void]$sb.AppendLine("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt, .release_glyph = NULL,")
[void]$sb.AppendLine(("    .line_height = {0}, .base_line = {1}," -f $lineHeight, $baseLine))
[void]$sb.AppendLine("    .subpx = LV_FONT_SUBPX_NONE, .kerning = LV_FONT_KERNING_NONE,")
[void]$sb.AppendLine("    .static_bitmap = 0, .underline_position = -2, .underline_thickness = 1,")
[void]$sb.AppendLine("    .dsc = &font_dsc, .fallback = NULL, .user_data = NULL,")
[void]$sb.AppendLine("};")
[System.IO.File]::WriteAllText($Out, $sb.ToString(), [System.Text.Encoding]::ASCII)
$gfx.Dispose(); $bmp.Dispose(); $emojiFont.Dispose(); $collection.Dispose()
Write-Output ("emoji font: {0} glyphs, {1} bytes bitmap, range U+{2:X}..U+{3:X}" -f $unicode.Count, $bitmapBytes.Count, $minCp, $maxCp)



