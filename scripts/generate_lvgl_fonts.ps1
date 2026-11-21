param(
  [string]$Root = (Join-Path $PSScriptRoot ".."),
  [string]$OutDir = (Join-Path $PSScriptRoot "..\platform\violet\ui\fonts")
)
# Rasterises Ubuntu and Material Icons into LVGL 9 lv_font_fmt_txt C files.
#
# The generated fonts carry the full Vietnamese repertoire (Latin-1, Latin
# Extended-A additions and U+1EA0..U+1EFF), because every label in the shell is
# Vietnamese. Each glyph is stored as plain 8-bit coverage (bpp 8) so LVGL
# blends it directly; no runtime decoding and no filesystem dependency.
#
#   powershell -ExecutionPolicy Bypass -File scripts/generate_lvgl_fonts.ps1
#
# Output: platform/violet/ui/fonts/vqeaf_font_*.c + vqeaf_fonts.h
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$Root = [System.IO.Path]::GetFullPath($Root)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
[void][System.IO.Directory]::CreateDirectory($OutDir)

$UbuntuRegular = Join-Path $Root "assets\fonts\Ubuntu-Regular.ttf"
$UbuntuBold = Join-Path $Root "assets\fonts\Ubuntu-Bold.ttf"
$MaterialIcons = Join-Path $Root "assets\fonts\MaterialIcons-Regular.ttf"

# --- glyph repertoire -------------------------------------------------------

$baseCodepoints = New-Object 'System.Collections.Generic.SortedSet[int]'
for($cp = 0x20; $cp -le 0x7E; $cp++) { [void]$baseCodepoints.Add($cp) }
for($cp = 0x00A0; $cp -le 0x00FF; $cp++) { [void]$baseCodepoints.Add($cp) }
foreach($cp in @(0x0102,0x0103,0x0110,0x0111,0x0128,0x0129,0x0168,0x0169,
                 0x01A0,0x01A1,0x01AF,0x01B0,0x20AB)) {
  [void]$baseCodepoints.Add($cp)
}
for($cp = 0x1EA0; $cp -le 0x1EFF; $cp++) { [void]$baseCodepoints.Add($cp) }
# Vietnamese text needs the en dash / bullet / degree / multiplication signs.
foreach($cp in @(0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,
                 0x00B0,0x00D7,0x2192,0x2190)) {
  [void]$baseCodepoints.Add($cp)
}

$digitCodepoints = New-Object 'System.Collections.Generic.SortedSet[int]'
foreach($cp in @(0x20,0x25,0x2B,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,
                 0x36,0x37,0x38,0x39,0x3A,0x41,0x43,0x46,0x48,0x4E,0x54,0x56)) {
  [void]$digitCodepoints.Add($cp)
}
foreach($cp in @(0x00B0,0x00B1,0x2013)) { [void]$digitCodepoints.Add($cp) }

# Icons used by the shell: status bar, navigation, apps and quick settings.
$iconNames = @(
  "wifi","signal_cellular_4_bar","battery_full","battery_charging_full",
  "search","home","apps","settings","lock","arrow_back","more_vert",
  "chevron_right","folder","photo_library","music_note","movie","camera_alt",
  "calendar_today","note","language","security","storefront","shop","phone",
  "message","contacts","sports_esports","calculate","mic","add","remove",
  "delete","play_arrow","pause","skip_next","skip_previous","thunderstorm",
  "water_drop","air","thermostat","wb_sunny","cloud","refresh","check","close",
  "edit","share","download","upload","star","favorite","volume_up","bluetooth",
  "airplanemode_active","flashlight_on","do_not_disturb_on","grid_view","list",
  "keyboard_arrow_down","send","power_settings_new","restart_alt","schedule",
  "info","storage","system_update","bolt","arrow_forward","open_in_new",
  "notifications","palette","translate","smartphone","memory","build",
  "folder_open","image","audiotrack","videocam","gamepad","lock_open",
  "person","group","verified_user","cloud_download","update","delete_sweep",
  "signal_wifi_off","bluetooth_disabled","brightness_6","headphones",
  "skip_previous","queue_music","content_copy","link","attach_file","save",
  "keyboard","text_fields","touch_app","spellcheck","aspect_ratio",
  "view_stream","push_pin","check_circle","keyboard_alt",
  "wifi_tethering","location_on","battery_saver","screen_rotation",
  "emergency","bedtime","wallpaper","widgets","done","network_cell",
  "contrast","mode","do_not_disturb_off",
  "shuffle","repeat","lyrics","all_inclusive",
  # IME special keys: keep captions for accessibility/tests and add glyphs
  # from Material Icons (Google Fonts) for a cleaner, icon-led key row.
  "backspace","keyboard_capslock","keyboard_return","numbers","space_bar",
  "arrow_upward","abc","keyboard_hide"
)
$iconCodepoints = New-Object 'System.Collections.Generic.SortedSet[int]'
$map = @{}
$cpFile = Join-Path $Root "assets\fonts\MaterialIcons-Regular.codepoints"
foreach($line in [System.IO.File]::ReadAllLines($cpFile)) {
  $parts = $line.Trim() -split '\s+'
  if($parts.Count -ge 2) { $map[$parts[0]] = [Convert]::ToInt32($parts[1], 16) }
}
$iconMacros = New-Object 'System.Collections.Generic.List[string]'
$iconSeen = New-Object 'System.Collections.Generic.HashSet[string]'
foreach($name in $iconNames) {
  if(-not $map.ContainsKey($name)) { continue }
  [void]$iconCodepoints.Add([int]$map[$name])
  if(-not $iconSeen.Add($name)) { continue }
  $bytes = [System.Text.Encoding]::UTF8.GetBytes([string][char][int]$map[$name])
  $escaped = ""
  foreach($byte in $bytes) { $escaped += ("\x{0:X2}" -f $byte) }
  $macro = "VQEAF_SYM_" + ($name.ToUpperInvariant() -replace '[^A-Z0-9]', '_')
  $iconMacros.Add(("#define {0,-30} `"{1}`"" -f $macro, $escaped))
}

# --- helpers ----------------------------------------------------------------

function Emit-GlyphFont {
  param(
    [string]$FontPath,
    [System.Drawing.FontStyle]$Style,
    [single]$Size,
    [System.Collections.Generic.SortedSet[int]]$Codepoints,
    [string]$Symbol,
    [string]$OutFile
  )

  $collection = New-Object System.Drawing.Text.PrivateFontCollection
  $collection.AddFontFile((Resolve-Path $FontPath).Path)
  $family = $collection.Families[0]
  $font = New-Object System.Drawing.Font($family, $Size, $Style, [System.Drawing.GraphicsUnit]::Pixel)
  $em = [single]$family.GetEmHeight($Style)
  $ascent = [int][Math]::Ceiling($family.GetCellAscent($Style) * $Size / $em)
  $descent = [int][Math]::Ceiling($family.GetCellDescent($Style) * $Size / $em)
  $pad = [int][Math]::Ceiling($Size * 0.35) + 1
  $width = [int][Math]::Ceiling($Size * 2.0) + $pad * 2
  $height = [int][Math]::Ceiling($Size * 1.8) + $pad * 2
  $bmp = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  $gfx.TextRenderingHint = if($Size -ge 30) {
    [System.Drawing.Text.TextRenderingHint]::AntiAlias
  } else {
    [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  }
  $fmt = [System.Drawing.StringFormat]::GenericTypographic.Clone()
  $fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip -bor `
                     [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces
  $brush = [System.Drawing.Brushes]::White
  $pt = New-Object System.Drawing.PointF([single]$pad, [single]$pad)
  $rect = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
  $lineHeight = $ascent + $descent + 2
  $baseLine = $descent + 2

  $bitmapBytes = New-Object 'System.Collections.Generic.List[byte]'
  $descs = New-Object 'System.Collections.Generic.List[string]'
  $descs.Add("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}, /* id = 0 reserved */")
  $unicode = New-Object 'System.Collections.Generic.List[int]'
  $minCp = 0x10FFFF
  $maxCp = 0
  $gid = 0

  foreach($cp in $Codepoints) {
    $gfx.Clear([System.Drawing.Color]::Black)
    $s = [string][char]$cp
    $gfx.DrawString($s, $font, $brush, $pt, $fmt)
    $advance = $gfx.MeasureString($s, $font, $pt, $fmt).Width

    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $bytes = New-Object byte[] ($stride * $height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)

    $left = $width; $right = -1; $top = $height; $bottom = -1
    for($y = 0; $y -lt $height; $y++) {
      $row = $y * $stride
      for($x = 0; $x -lt $width; $x++) {
        if($bytes[$row + $x * 4 + 2] -gt 0) {
          if($x -lt $left) { $left = $x }
          if($x -gt $right) { $right = $x }
          if($y -lt $top) { $top = $y }
          if($y -gt $bottom) { $bottom = $y }
        }
      }
    }

    $boxW = 0; $boxH = 0; $ofsX = 0; $ofsY = 0
    if($right -ge 0) {
      $boxW = $right - $left + 1
      $boxH = $bottom - $top + 1
      $ofsX = $left - $pad
      # LVGL places a glyph box with
      #   top = baseline - box_h - ofs_y
      # so ofs_y is measured from the baseline. Deriving it from the bitmap
      # top instead pushes every glyph to cap height and moves commas, tails
      # and Vietnamese tone dots above the letters.
      $ofsY = ($pad + $ascent) - $bottom - 1
      for($y = 0; $y -lt $boxH; $y++) {
        $row = ($top + $y) * $stride
        for($x = 0; $x -lt $boxW; $x++) {
          $bitmapBytes.Add($bytes[$row + ($left + $x) * 4 + 2])
        }
      }
    }

    $advW = [int][Math]::Round($advance * 16.0)
    $bitmapIndex = $bitmapBytes.Count - ($boxW * $boxH)
    $descs.Add(("    {{.bitmap_index = {0}, .adv_w = {1}, .box_w = {2}, .box_h = {3}, .ofs_x = {4}, .ofs_y = {5}}}, /* U+{6:X4} */" -f `
      $bitmapIndex, $advW, $boxW, $boxH, $ofsX, $ofsY, $cp))
    $unicode.Add($cp)
    if($cp -lt $minCp) { $minCp = $cp }
    if($cp -gt $maxCp) { $maxCp = $cp }
    $gid++
  }

  $sb = New-Object System.Text.StringBuilder
  [void]$sb.AppendLine("/* Generated from $([System.IO.Path]::GetFileName($FontPath)) at ${Size}px; do not edit by hand. */")
  [void]$sb.AppendLine("/* Rasterised by scripts/generate_lvgl_fonts.ps1. 8-bit coverage, LVGL 9 fmt_txt. */")
  [void]$sb.AppendLine("#include `"lvgl.h`"")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("#ifndef LV_ATTRIBUTE_LARGE_CONST")
  [void]$sb.AppendLine("#define LV_ATTRIBUTE_LARGE_CONST")
  [void]$sb.AppendLine("#endif")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
  for($i = 0; $i -lt $bitmapBytes.Count; $i += 16) {
    $n = [Math]::Min(16, $bitmapBytes.Count - $i)
    $chunk = New-Object string[] $n
    for($k = 0; $k -lt $n; $k++) { $chunk[$k] = "{0}" -f $bitmapBytes[$i + $k] }
    [void]$sb.AppendLine("    " + ([string]::Join(",", $chunk)) + ",")
  }
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("static const uint16_t unicode_list_0[] = {")
  for($i = 0; $i -lt $unicode.Count; $i += 12) {
    $n = [Math]::Min(12, $unicode.Count - $i)
    $chunk = New-Object string[] $n
    for($k = 0; $k -lt $n; $k++) { $chunk[$k] = "0x{0:X}" -f ($unicode[$i + $k] - $minCp) }
    [void]$sb.AppendLine("    " + ([string]::Join(",", $chunk)) + ",")
  }
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
  foreach($d in $descs) { [void]$sb.AppendLine($d) }
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
  [void]$sb.AppendLine(("    {{.range_start = {0}, .range_length = {1}, .glyph_id_start = 1," -f $minCp, ($maxCp - $minCp + 1)))
  [void]$sb.AppendLine("     .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL,")
  [void]$sb.AppendLine(("     .list_length = {0}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}" -f $unicode.Count))
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("static const lv_font_fmt_txt_dsc_t font_dsc = {")
  [void]$sb.AppendLine("    .glyph_bitmap = glyph_bitmap,")
  [void]$sb.AppendLine("    .glyph_dsc = glyph_dsc,")
  [void]$sb.AppendLine("    .cmaps = cmaps,")
  [void]$sb.AppendLine("    .kern_dsc = NULL,")
  [void]$sb.AppendLine("    .kern_scale = 0,")
  [void]$sb.AppendLine("    .cmap_num = 1,")
  [void]$sb.AppendLine("    .bpp = 8,")
  [void]$sb.AppendLine("    .kern_classes = 0,")
  [void]$sb.AppendLine("    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,")
  [void]$sb.AppendLine("    .stride = 0,")
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("const lv_font_t $Symbol = {")
  [void]$sb.AppendLine("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
  [void]$sb.AppendLine("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
  [void]$sb.AppendLine("    .release_glyph = NULL,")
  [void]$sb.AppendLine(("    .line_height = {0}," -f $lineHeight))
  [void]$sb.AppendLine(("    .base_line = {0}," -f $baseLine))
  [void]$sb.AppendLine("    .subpx = LV_FONT_SUBPX_NONE,")
  [void]$sb.AppendLine("    .kerning = LV_FONT_KERNING_NONE,")
  [void]$sb.AppendLine("    .static_bitmap = 0,")
  [void]$sb.AppendLine("    .underline_position = -2,")
  [void]$sb.AppendLine("    .underline_thickness = 1,")
  [void]$sb.AppendLine("    .dsc = &font_dsc,")
  [void]$sb.AppendLine("    .fallback = NULL,")
  [void]$sb.AppendLine("    .user_data = NULL,")
  [void]$sb.AppendLine("};")
  [System.IO.File]::WriteAllText($OutFile, $sb.ToString(), [System.Text.Encoding]::ASCII)

  $gfx.Dispose(); $bmp.Dispose(); $font.Dispose(); $collection.Dispose()
  Write-Output ("  {0}: {1} glyphs, {2} bytes bitmap, {3} px line" -f `
    [System.IO.Path]::GetFileName($OutFile), $gid, $bitmapBytes.Count, $lineHeight)
}

# --- emit -------------------------------------------------------------------

Write-Output "generate_lvgl_fonts: Ubuntu regular body faces"
$body = @(
  @{size = 20; name = "vqeaf_font_20"},
  @{size = 24; name = "vqeaf_font_24"},
  @{size = 28; name = "vqeaf_font_28"},
  @{size = 34; name = "vqeaf_font_34"}
)
foreach($face in $body) {
  Emit-GlyphFont -FontPath $UbuntuRegular -Style ([System.Drawing.FontStyle]::Regular) `
    -Size $face.size -Codepoints $baseCodepoints -Symbol $face.name `
    -OutFile (Join-Path $OutDir ($face.name + ".c"))
}

Write-Output "generate_lvgl_fonts: Ubuntu bold body faces (weight system)"
foreach($face in @(@{size = 20; name = "vqeaf_font_bold_20"},
                   @{size = 24; name = "vqeaf_font_bold_24"},
                   @{size = 28; name = "vqeaf_font_bold_28"},
                   @{size = 34; name = "vqeaf_font_bold_34"})) {
  Emit-GlyphFont -FontPath $UbuntuBold -Style ([System.Drawing.FontStyle]::Bold) `
    -Size $face.size -Codepoints $baseCodepoints -Symbol $face.name `
    -OutFile (Join-Path $OutDir ($face.name + ".c"))
}

Write-Output "generate_lvgl_fonts: Ubuntu bold display faces"
foreach($face in @(@{size = 56; name = "vqeaf_font_bold_56"},
                   @{size = 96; name = "vqeaf_font_bold_96"})) {
  Emit-GlyphFont -FontPath $UbuntuBold -Style ([System.Drawing.FontStyle]::Bold) `
    -Size $face.size -Codepoints $digitCodepoints -Symbol $face.name `
    -OutFile (Join-Path $OutDir ($face.name + ".c"))
}

Write-Output "generate_lvgl_fonts: Material Icons"
foreach($face in @(@{size = 28; name = "vqeaf_icon_28"},
                   @{size = 40; name = "vqeaf_icon_40"})) {
  Emit-GlyphFont -FontPath $MaterialIcons -Style ([System.Drawing.FontStyle]::Regular) `
    -Size $face.size -Codepoints $iconCodepoints -Symbol $face.name `
    -OutFile (Join-Path $OutDir ($face.name + ".c"))
}

$header = @(
  "/* Generated by scripts/generate_lvgl_fonts.ps1; do not edit by hand. */",
  "#ifndef VQEAF_LVGL_FONTS_H",
  "#define VQEAF_LVGL_FONTS_H",
  "",
  "LV_FONT_DECLARE(vqeaf_font_20);",
  "LV_FONT_DECLARE(vqeaf_font_24);",
  "LV_FONT_DECLARE(vqeaf_font_28);",
  "LV_FONT_DECLARE(vqeaf_font_34);",
  "LV_FONT_DECLARE(vqeaf_font_bold_20);",
  "LV_FONT_DECLARE(vqeaf_font_bold_24);",
  "LV_FONT_DECLARE(vqeaf_font_bold_28);",
  "LV_FONT_DECLARE(vqeaf_font_bold_34);",
  "LV_FONT_DECLARE(vqeaf_font_bold_56);",
  "LV_FONT_DECLARE(vqeaf_font_bold_96);",
  "LV_FONT_DECLARE(vqeaf_icon_28);",
  "LV_FONT_DECLARE(vqeaf_icon_40);",
  "",
  "/* Typography weights. VQEAF_FONT_* resolve through vqeaf_type_font() so the",
  " * whole shell honours the bold-text accessibility mode; *_BOLD pins a face. */",
  "typedef enum {",
  "    VQEAF_ROLE_CAPTION = 0,",
  "    VQEAF_ROLE_BODY,",
  "    VQEAF_ROLE_TITLE,",
  "    VQEAF_ROLE_HEADLINE",
  "} vqeaf_font_role_t;",
  "",
  "typedef enum {",
  "    VQEAF_WEIGHT_REGULAR = 0,",
  "    VQEAF_WEIGHT_BOLD = 1",
  "} vqeaf_font_weight_t;",
  "",
  "const lv_font_t *vqeaf_type_font(vqeaf_font_role_t role);",
  "const lv_font_t *vqeaf_type_font_weight(vqeaf_font_role_t role,",
  "                                       vqeaf_font_weight_t weight);",
  "void vqeaf_type_set_bold(int on);",
  "int vqeaf_type_get_bold(void);",
  "",
  "#define VQEAF_FONT_CAPTION   (vqeaf_type_font(VQEAF_ROLE_CAPTION))",
  "#define VQEAF_FONT_BODY      (vqeaf_type_font(VQEAF_ROLE_BODY))",
  "#define VQEAF_FONT_TITLE     (vqeaf_type_font(VQEAF_ROLE_TITLE))",
  "#define VQEAF_FONT_HEADLINE  (vqeaf_type_font(VQEAF_ROLE_HEADLINE))",
  "#define VQEAF_FONT_CAPTION_BOLD  (vqeaf_type_font_weight(VQEAF_ROLE_CAPTION, VQEAF_WEIGHT_BOLD))",
  "#define VQEAF_FONT_BODY_BOLD     (vqeaf_type_font_weight(VQEAF_ROLE_BODY, VQEAF_WEIGHT_BOLD))",
  "#define VQEAF_FONT_TITLE_BOLD    (vqeaf_type_font_weight(VQEAF_ROLE_TITLE, VQEAF_WEIGHT_BOLD))",
  "#define VQEAF_FONT_HEADLINE_BOLD (vqeaf_type_font_weight(VQEAF_ROLE_HEADLINE, VQEAF_WEIGHT_BOLD))",
  "#define VQEAF_FONT_HOME_CLOCK (&vqeaf_font_bold_56)",
  "#define VQEAF_FONT_CLOCK     (&vqeaf_font_bold_96)",
  "#define VQEAF_ICON_SM        (&vqeaf_icon_28)",
  "#define VQEAF_ICON           (&vqeaf_icon_40)",
  "",
  "/* Material Icons codepoints used by the shell. */"
) + $iconMacros + @(
  "",
  "#endif"
)
[System.IO.File]::WriteAllLines((Join-Path $OutDir "vqeaf_fonts.h"), $header, [System.Text.Encoding]::ASCII)
Write-Output "generate_lvgl_fonts: done -> $OutDir"
