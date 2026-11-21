#!/bin/bash
CFG=/root/qeaf-mt689x/out/chopin/.config
SRC=${SRC:-$HOME/qeaf-mt689x/linux}
cd "$SRC"
echo "== BT / SCP related config"
grep -E '^CONFIG_(BT|BT_HCI_UART|BT_MTK|MTK_BTIF|MTK_COMBO_BT|MTK_SCP_CHOPIN|MTK_TINYSYS_SCP_SUPPORT|MTK_SENSORHUB|MTK_IPI|MTK_MBOX|MTK_SCP_IPI|SCP|MTK_SCP_RV)=' "$CFG"
echo "== where mtk_ipi_register / mtk_mbox_read defined"
grep -rn 'int mtk_ipi_register\|void mtk_ipi_send\|mtk_mbox_read\|ipi_monitor_dump\|mtk_ipi_tracking' --include=*.c --include=*.h drivers | head -25
echo "== their Makefiles"
for f in $(grep -rln 'int mtk_ipi_register\|mtk_mbox_read' --include=*.c drivers | head -5); do echo "-- $f"; grep -n 'obj-' $(dirname $f)/Makefile | head -10; done
echo "== Kconfig symbols"
grep -rn -B3 -A8 'config MTK_IPI\|config MTK_MBOX\|config MTK_SCP' drivers/misc/mediatek/Kconfig drivers/soc/mediatek/Kconfig 2>/dev/null | head -60
