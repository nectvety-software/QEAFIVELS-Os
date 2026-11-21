# QEAFIVELS OS — Violet WCN3990 native driver

## Porting source

The crDroid Violet device.mk (Android 14 branch) configures the Android product layer, not a standalone kernel driver. Its Wi-Fi section requests android.hardware.wifi-service, hostapd, wpa_supplicant, libwifi-hal-qcom, and WCNSS configuration files. The corresponding board configuration selects qcwcn, qca_cld3, and NL80211. QEAF does not import Android HAL, HIDL, Binder, or Android init services.

Hardware reference: the Qualcomm WCN3990 device-tree binding identifies qcom,wcn3990-wifi as an integrated SNOC device, with an MMIO resource and copy-engine interrupts. Violet's existing stock-kernel diagnostics identify the WLAN platform device at 0x18800000 and the icnss/WLAN stack. See:
https://android.googlesource.com/kernel/msm/+/ef85d528056e472b667607601cc6a7f7cccb82b1/Documentation/devicetree/bindings/net/wireless/qcom,wcn3990-wifi.txt

## Native driver layer added

- kernel/include/vqeaf/wcn3990.h
- kernel/drivers/wcn3990_snoc.c
- tests/wcn3990_driver_smoke.c

Driver model name: qcom-wcn3990-snoc (device class RADIO).

Probe sequence is fail-closed:
1. Validate DT-provided compatible, SNOC bus, MMIO range, 12 CE IRQ resources, and MSA memory range.
2. Map registers through the platform backend.
3. Request CE interrupts.
4. Load the real wlanmdsp.mbn firmware and WCNSS_qcom_cfg.ini board config. The backend must also verify real board data/calibration; -ENODATA blocks the Wi-Fi capability.
5. Initialize copy engines.
6. Start the real WLFW/QMI control plane.
7. Register the network interface.
8. Publish radio.wifi=READY only after every stage succeeds.

On any failure, the backend shutdown callback is invoked with the last completed stage. No calibration, MAC address, firmware response, QRTR endpoint, or WLAN netdev is fabricated.

## Important implementation boundary

This is the native OS driver contract and lifecycle layer, not yet a complete WCN3990 hardware implementation. The Violet AArch64 kernel still needs:
- Qualcomm SNOC/ICNSS power and clock sequencing,
- MSA memory reservation and secure handoff,
- CE register/interrupt/DMA implementation,
- QRTR/QMI transport and WLFW protocol implementation,
- firmware/board-data loader wired to a trusted factory calibration source,
- network device and 802.11 data path (TX/RX).

Until those platform callbacks are implemented and bound from a real Violet device-tree node, this driver must remain unbound and radio.wifi must not be reported READY. The smoke test uses explicit mock callbacks only to verify lifecycle ordering and rollback; it is not hardware evidence.

## Build and tests

- scripts/build_kernel_aarch64.py compiles the driver into the independent AArch64 kernel image.
- vqeaf_wcn3990_smoke verifies success ordering, failure rollback, and rejection of a mismatched bus type.
