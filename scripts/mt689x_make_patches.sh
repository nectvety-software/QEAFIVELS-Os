#!/bin/bash
# Produce the LM36273 DT-compatible fix as a patch file in the QEAF repo,
# generated from the real kernel tree (not hand-written), then restore the tree.
set -euo pipefail
SRC="${SRC:-$HOME/qeaf-mt689x/linux}"
OUT=/mnt/d/QEAFIVELS-Os/platform/mt689x/kernel/patches
FILE=drivers/gpu/drm/panel/leds-lm36273.c

cd "$SRC"
mkdir -p "$OUT"

if grep -q 'mediatek,i2c_lcd_bias' "$FILE"; then
  echo "kernel tree already patched; skipping patch generation"
  exit 0
fi

python3 - "$FILE" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
old = '\t{ .compatible = "mediatek,I2C_LCD_BIAS", },\n'
new = ('\t{ .compatible = "mediatek,I2C_LCD_BIAS", },\n'
       '\t/* The chopin DTS spells it lowercase; of matching is exact. */\n'
       '\t{ .compatible = "mediatek,i2c_lcd_bias", },\n')
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
old_id = '\t{ "I2C_LCD_BIAS", 0 },\n'
new_id = '\t{ "I2C_LCD_BIAS", 0 },\n\t{ "i2c_lcd_bias", 0 },\n'
assert s.count(old_id) == 1
s = s.replace(old_id, new_id)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
PY

git diff -- "$FILE" > "$OUT/0001-lm36273-accept-lowercase-dt-compatible.patch"
git checkout -- "$FILE"

echo "wrote $OUT/0001-lm36273-accept-lowercase-dt-compatible.patch"
git apply --check "$OUT/0001-lm36273-accept-lowercase-dt-compatible.patch"
echo "patch applies cleanly"
