#!/bin/bash
# Generate patch 0002: export the two AFE clock helpers the MT6893 machine
# driver calls. They live in mt8192-afe-clk.c (part of snd-soc-mt8192-afe);
# the machine card is a separate module, so without EXPORT_SYMBOL modpost
# fails with "symbol 'mt8192_mck_enable' undefined".
set -euo pipefail
SRC="${SRC:-$HOME/qeaf-mt689x/linux}"
OUT=/mnt/d/QEAFIVELS-Os/platform/mt689x/kernel/patches
FILE=sound/soc/mediatek/mt8192/mt8192-afe-clk.c

cd "$SRC"
mkdir -p "$OUT"

if grep -q 'EXPORT_SYMBOL_GPL(mt8192_mck_enable)' "$FILE"; then
  echo "already exported; skipping"
  exit 0
fi

python3 - "$FILE" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()

old_enable = "\treturn 0;\n}\n"
# Anchor on the exact function tail: find mt8192_mck_disable definition and
# append exports right after it, which covers both helpers.
marker = "void mt8192_mck_disable(struct mtk_base_afe *afe, int mck_id)\n{"
i = s.index(marker)
# find the closing brace of that function
j = s.index("\n}\n", i) + len("\n}\n")
add = ("\n/* The MT6893 machine card is a separate module (SND_SOC_MT6893_MT6359=m)\n"
       " * and calls these two helpers from its hw_params/free paths. The vendor\n"
       " * tree always built the AFE in, so they were never exported. */\n"
       "EXPORT_SYMBOL_GPL(mt8192_mck_enable);\n"
       "EXPORT_SYMBOL_GPL(mt8192_mck_disable);\n")
s = s[:j] + add + s[j:]
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
PY

git diff -- "$FILE" > "$OUT/0002-mt8192-afe-export-mck-helpers.patch"
git checkout -- "$FILE"
echo "wrote $OUT/0002-mt8192-afe-export-mck-helpers.patch"
git apply --check "$OUT/0002-mt8192-afe-export-mck-helpers.patch"
echo "patch applies cleanly"
