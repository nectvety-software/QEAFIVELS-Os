# QEAFIVELS OS — Violet WCN3990 Phase 2 (SNOC / ICNSS / Copy Engine)

## Implemented in this phase

- Native driver lifecycle for the WCN3990 SNOC platform.
- Strict resource validation: DT-supplied SNOC MMIO, MSA region and 12 distinct CE IRQs. No production address or IRQ is guessed by the driver.
- Explicit power-up order: map SNOC → regulators → clocks → ICNSS power vote → MSA preparation → CE IRQ registration.
- CE0–CE11 ring sizing follows the upstream WCN3990 host CE configuration. Each ring is 8-byte aligned, zeroed, DMA-allocated and checked against the 37-bit DMA aperture.
- Per-CE hardware configuration is delegated to a Violet platform backend; reverse-order unwind covers partial probe and remove.
- Capability split: `radio.wifi.snoc_ce=READY` means Phase 2 transport resources are initialized; `radio.wifi` remains PRESENT and is not reported READY.

## Explicitly not enabled yet

No QMI, QRTR, WLFW, firmware boot, netdev or 802.11 TX/RX is started. Destination rings are allocated/configured but RX buffers are not posted. The current AArch64 kernel is still a QEMU-oriented port: it has no Violet DTB parser, Qualcomm regulator/clock providers, ICNSS backend, dynamic GIC SPI allocator, IOMMU/SMMU mapping or coherent DMA allocator. Therefore the driver is compiled and host-tested but deliberately not bound to the phone.

The callbacks are hardware contracts, not fake hardware. The next hardware-enablement tasks are: implement the Violet DTB parser; wire PMIC regulators and clocks; implement ICNSS/remoteproc and MSA ownership; implement GIC SPI registration; implement coherent DMA/IOMMU; then add the CE register programming and IRQ acknowledge logic using the target's exact CE register map. Only after live CE interrupt/ring tests should Phase 3 QMI/WLFW begin.

## References

- Qualcomm WCN3990 binding describes the integrated WLAN as SNOC and lists MMIO + CE interrupt resources: https://android.googlesource.com/kernel/msm/+/ef85d528056e472b667607601cc6a7f7cccb82b1/Documentation/devicetree/bindings/net/wireless/qcom,wcn3990-wifi.txt
- Upstream ath10k SNOC source provides the 12-engine host ring profile and WCN3990 shadow-index details: https://github.com/torvalds/linux/blob/master/drivers/net/wireless/ath/ath10k/snoc.c
- Upstream ath10k CE source describes source/destination rings and descriptor semantics: https://android.googlesource.com/kernel/common/+/913a89f009d98c85a902d718cd54bb32ab11d167/drivers/net/wireless/ath/ath10k/ce.c

All source references are used as hardware documentation; this driver does not import Android HAL/HIDL/Binder.
