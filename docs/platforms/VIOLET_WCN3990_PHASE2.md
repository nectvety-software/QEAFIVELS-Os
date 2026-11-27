# QEAFIVELS OS — Violet WCN3990 Phase 2 (SNOC / ICNSS / Copy Engine)

## Implemented in this phase

- Native driver lifecycle for the WCN3990 SNOC platform.
- Strict resource validation: DT-supplied SNOC MMIO, MSA region and 12 distinct CE IRQs. No production address or IRQ is guessed by the driver.
- Untrusted FDT traversal is depth-bounded at 32 levels. The legacy contiguous-IRQ ABI is used only after every parsed CE IRQ is verified contiguous and overflow-safe.
- Explicit power-up order: map SNOC → regulators → clocks → ICNSS power vote → MSA preparation → CE IRQ registration.
- CE0–CE11 ring sizing follows the upstream WCN3990 host CE configuration. Each ring is 8-byte aligned, zeroed and DMA-allocated. Both CE ring-base registers are 32-bit; the backend rejects a base at or above `2^32` and any ring extent that crosses that boundary.
- A native CE backend (ABI 2) programs the verified WCN3990 source/destination base, size, CTRL1, watermark and interrupt registers through injected MMIO callbacks. It never dereferences a physical address and never programs a fabricated base-high register.
- The MMIO provider explicitly supplies `dma_bits` and `address_high_mask` for descriptor DMA/IOMMU metadata, not for CE ring bases. Upstream `0x10`/`0x12` values are CTRL1 byte-swap bit positions, not MMIO offsets. A host fixture retains a valid 35-bit descriptor aperture with mask `0x7` while proving source and destination ring success at `2^32 - 16` and rejection at `2^32`.
- Ring initialization checks online indices, preserves unrelated read-modify-write register bits, leaves indices untouched and masks CE interrupts until a real service is installed.
- Failed configuration attempts every required register cleanup and returns the original error only if cleanup succeeds. If cleanup fails, the CE remains in `_quiesce_mask`; `ce_disable` reports the error until a later cleanup succeeds.
- Phase 2 ABI 3 stops reverse-order release at the first `ce_disable` failure. It retains the failed CE and lower-index DMA allocations, IRQs, MSA, power and mapping, sets `quiesce_pending`, and rejects further binds with `-EBUSY` until reboot/reset. No DMA ring is freed while its CE may still reference it. The Phase 2 fixture proves CE11 is disabled and its destination ring freed before CE10 fails, after which every lower resource remains retained.
- Copy-complete/watermark acknowledgement is implemented. DDR RRI for `CE_ATTR_DIS_INTR` is not implemented, so live index queries and interrupt enable are rejected for poll-mode CEs.
- Capability split: `radio.wifi.snoc_ce=READY` means Phase 2 transport resources are initialized; `radio.wifi` remains PRESENT and is not reported READY.

## Explicitly not enabled yet

No QMI, QRTR, WLFW, firmware boot, netdev or 802.11 TX/RX is started. Destination rings are allocated/configured but RX buffers are not posted. The Violet DTB parser now resolves the real SNOC, MSA, SMMU, regulator and CE IRQ resources, but the production platform backend still has no MMIO mapping, regulator, clock, ICNSS, IRQ lifecycle, IOMMU/SMMU or coherent DMA provider.

The Violet CE context is deliberately left unattached. Its Phase-2 `ce_configure` delegation returns `-ENOSYS`, and a real-DTB bind still fails earlier at `map_snoc` with `-ENOSYS`. Compiling the backend or parsing the DTB does not enable hardware access or publish Wi-Fi readiness.

The repository's 37-bit Phase-2 descriptor DMA aperture differs from the 35-bit SNOC setting in the inspected upstream source. This does not widen the CE ring-base registers: both are 32-bit and have no base-high register or production high-address mask. The descriptor aperture must be reconciled with target documentation and the actual DMA/IOMMU provider before any physical attachment.

The next hardware-enablement tasks are capability-mediated SNOC mapping, PMIC regulators/clocks, ICNSS/remoteproc and MSA ownership, GIC SPI registration with disable/free support, coherent DMA/IOMMU, and DDR RRI/poll-mode support. Only after explicitly authorized live CE ring/interrupt tests should Phase 3 QMI/WLFW begin.

## Host verification (2026-11-24)

- Strict Zig host compile with `-Wall -Wextra -Werror -UNDEBUG` and `wcn3990_ce_smoke: PASS`, including persistent MMIO cleanup failures, retry-to-success, quiesce retention, no accesses to raw offsets `0x10`/`0x12`, and the 32-bit source/destination ring boundary.
- The same CE fixture passes Zig `-fsanitize=undefined -fno-sanitize-recover=all`.
- `ctest --test-dir build-native -R vqeaf_wcn3990 --output-on-failure`: 3/3 PASS.
- `ctest --test-dir build-native --output-on-failure`: 18/18 PASS after rebuilding all native targets.
- `python scripts/wifi/run_violet_platform_tests.py`: `vqeaf_violet_platform: ALL OK` against the real reference DTB, including fail-closed `-ENOSYS`, rejection of 64-level DTB nesting, and rejection of noncontiguous legacy IRQs. Strict `-Werror` and UBSan variants also pass.
- `python scripts/wifi/run_wcn3990_state_tests.py`: lifecycle rollback/timeout PASS.
- `python scripts/wifi/run_hal_tests.py`: HAL, QRTR/QMI, firmware boot and real firmware-container checks PASS.
- `python -m unittest discover -s tests/wifi -v`: 24/24 PASS; Violet bring-up contracts: 15/15 PASS.
- `python scripts/test_kernel_aarch64.py`: rebuilt and linked AArch64 ELF, 651984 bytes, SHA-256 `6cee6b7a56684978c7d5f014fe5e9111516173150f3cc47606bdff3c15006431`; QEMU virt boot, MMU, GIC/timer, EL0, IPC, launcher, compositor and virtio-block gates ALL OK.
- `python scripts/build_violet.py` and `python scripts/package_violet.py --skip-build`: PID1 build, boot-image validation and rescue-channel audit PASS; image SHA-256 `574c606aeb8dfa7f72810bdcc9aa00a5c87f3f90caa1d65d4a5246770c141337`.
- Host CE test SHA-256 `acdc26aa5acb6f24318d54501ae60d8cc59991a28fb9bfc3ef1dc3880da6481e`.

These are host/build/emulated evidence only. The packaged Violet image was not flashed. No physical Violet Wi-Fi, modem, flash or partition write was performed in this verification.

## References

- Qualcomm WCN3990 binding describes the integrated WLAN as SNOC and lists MMIO + CE interrupt resources: https://android.googlesource.com/kernel/msm/+/ef85d528056e472b667607601cc6a7f7cccb82b1/Documentation/devicetree/bindings/net/wireless/qcom,wcn3990-wifi.txt
- Upstream ath10k SNOC source provides the 12-engine host ring profile and WCN3990 shadow-index details: https://github.com/torvalds/linux/blob/master/drivers/net/wireless/ath/ath10k/snoc.c
- Upstream ath10k CE source describes source/destination rings and descriptor semantics: https://android.googlesource.com/kernel/common/+/913a89f009d98c85a902d718cd54bb32ab11d167/drivers/net/wireless/ath/ath10k/ce.c
- Upstream ath10k hardware map provides the WCN3990 CE bases, register offsets and masks: https://android.googlesource.com/kernel/common/+/913a89f009d98c85a902d718cd54bb32ab11d167/drivers/net/wireless/ath/ath10k/hw.c

All source references are used as hardware documentation; this driver does not import Android HAL/HIDL/Binder.
