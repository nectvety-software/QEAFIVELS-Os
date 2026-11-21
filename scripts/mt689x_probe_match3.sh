#!/bin/bash
cd "${SRC:-$HOME/qeaf-mt689x/linux}"
echo "== mfd add cells"
grep -n -B5 -A25 'mfd_add_devices' drivers/mfd/mt6360-core.c | head -60
echo "== tcpc driver type"
grep -n 'i2c_driver\|platform_driver\|module_i2c_driver\|module_platform_driver' drivers/usb/typec/tcpm/tcpci_mt6360.c
echo "== tcpc probe signature"
grep -n -B3 -A8 'static int tcpci_mt6360_probe' drivers/usb/typec/tcpm/tcpci_mt6360.c | head -20
echo "== mfd kconfig"
grep -n -A10 'config MFD_MT6360' drivers/mfd/Kconfig
echo "== dts mt6360 parent (i2c bus?)"
grep -n -B12 'mt6360_pmu_dts' arch/arm64/boot/dts/mediatek/mt6893-xiaomi-chopin.dts | head -20
