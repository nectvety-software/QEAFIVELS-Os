#!/bin/bash
CFG=/root/qeaf-mt689x/out/chopin/.config
SRC=${SRC:-$HOME/qeaf-mt689x/linux}
cd "$SRC"
echo "== configs (including 'is not set')"
grep -E 'CONFIG_(BT|MTK_MBOX|MTK_IPI|RPMSG_MTK_MBOX|RPMSG|MTK_SCP_CHOPIN|MTK_TINYSYS_SCP_SUPPORT|MTK_SENSORHUB)' "$CFG" | head -25
echo "== soc Makefile gating"
grep -nE 'mtk-mbox|mtk_tinysys_ipi|MTK_MBOX|MTK_IPI' drivers/soc/mediatek/Makefile
echo "== rpmsg Makefile"
grep -nE 'MTK_MBOX|mtk' drivers/rpmsg/Makefile
echo "== BT menuconfig"
grep -n -A10 -E '^(menu)?config BT$' net/bluetooth/Kconfig | head -20
echo "== which SCP config enables IPI"
grep -rn 'MTK_IPI\|MTK_MBOX' drivers/misc/mediatek/scp/Kconfig drivers/misc/mediatek/Kconfig 2>/dev/null | head -20
