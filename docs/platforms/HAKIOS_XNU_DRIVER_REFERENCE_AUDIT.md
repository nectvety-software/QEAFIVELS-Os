# HakiOS XNU driver reference audit for QEAFIVELS Violet

Date: 2026-11-23

## Scope

Reference inspected:
- https://github.com/oosexgang/hakios-xnu-kernel/tree/common/drivers
- Inspected revision: 62e09f850f1f713192228cb9031da256033ede93
- Relevant files: `drivers/board/drivers.c`, `drivers/board/soc.c`, `drivers/board/ipc.h`, `drivers/board/mdt.h`, `drivers/board/qrtr_frame.h`, `drivers/board/wifi.c`, `drivers/board/wlfw.c`, `drivers/board/lx.c`, `drivers/board/Kconfig`, `Documentation/drivers.md`.

## Critical compatibility finding

The inspected HakiOS Wi-Fi implementation is explicitly for **WCN6750/WPSS**, not WCN3990. Its driver configuration is `CONFIG_WIFI_WCN6750`; its firmware flow uses `wpss.mdt` and `wpss.b02...`; its WLAN control is based on the WCN6750/ath11k WLFW message set. The file `drivers/board/wlfw.c` explicitly calls itself a temporary stand-in to be replaced by ath11k's QMI implementation.

Therefore:
- Do NOT copy HakiOS WCN6750 register offsets, memory maps, IRQ values, CE pipe tables, firmware names, calibration assumptions, QMI message IDs, or boot sequence constants into Violet.
- Do NOT treat the HakiOS WLFW stand-in as a production implementation.
- Reuse only general design patterns after independently validating them against Violet's WCN3990 hardware, device tree, firmware package, and compatible upstream/downstream sources.

## Reusable engineering patterns

1. **Single ordered driver-init registry** — board/SoC discovery first, common resources next, peripherals after their dependencies. A driver's init entry must be gated by an explicit board capability and build option.
2. **Shared Qualcomm SoC services** — centralize AOP mailbox, shared-memory allocation/locking, RPMh sequencing, and SMMU context allocation where multiple remote processors share those resources. Do not duplicate these mechanisms in Wi-Fi/audio/camera drivers.
3. **Stepwise bring-up with read-before-write diagnostics** — each stage reports the resource it is about to inspect, records the observed value, validates prerequisites, and stops at the first failed stage. A diagnostic stage must be read-only unless an explicitly approved bring-up stage needs a write.
4. **Remote-processor lifecycle** — model power, clock, memory, firmware authentication/start, remote-ready indication, transport setup, service discovery, and shutdown as distinct states. Every failure path unwinds only resources that were successfully acquired.
5. **Firmware container validation** — parse Qualcomm MDT/ELF metadata with strict bounds checks; validate segment count, file offsets, memory ranges, hash/certificate region, overlap, and reserved-memory containment before handing data to secure firmware. Do not bypass TrustZone authentication.
6. **Transport separation** — keep GLINK/SMD transport, QRTR framing, QMI codec, and service-specific WLFW messages in separate layers. A valid QRTR packet is not proof that a QMI service is available.
7. **SMMU and DMA ownership** — establish device-visible mappings only after the IOMMU/SMMU is initialized; enforce DMA address width, alignment, coherency and lifetime; revoke mappings before releasing memory or powering down the remote processor.
8. **Stop/quiesce path** — stop asynchronous workers, prevent new requests, close transport, stop remote firmware, revoke DMA mappings, release IRQs and power resources in reverse order. Handle panic/loader return as a separate quiesce path.
9. **Build-time driver selection** — make unsupported hardware drivers unavailable by explicit configuration rather than compiling a fake success path.
10. **Evidence-oriented documentation** — document each driver as source-audited, ported, built, host-tested, emulator-tested, hardware-detected, hardware-initialized, or functionally verified.

## QEAFIVELS current source audit

Current QEAF tree contains:
- `kernel/drivers/wcn3990_snoc.c`
- `kernel/drivers/wcn3990_phase2.c`
- `kernel/drivers/wifi/wcn3990/wifi_device.c`
- `kernel/drivers/wifi/wcn3990/wifi_firmware.c`
- `kernel/drivers/wifi/wcn3990/firmware_boot.c`
- `kernel/drivers/wifi/wcn3990/qrtr_transport.c`
- `kernel/drivers/wifi/wcn3990/qmi_service.c`

The current implementation has useful separations for DT discovery, firmware structure validation, QRTR control/service discovery, QMI request/response, boot state transitions, and SNOC/CE resource lifecycle. However, callback-driven host tests do not prove a physical Violet backend exists or works. The Phase 2 source itself documents that board-specific register, regulator, clock, GIC and DMA operations are supplied by a future Violet backend.

Important next engineering work:
- Build a verified Violet board resource provider from the actual device tree and platform description. Reject missing/ambiguous resources; do not infer IRQ ranges or MSA addresses.
- Replace callback-only platform seams with real QEAF MMIO, regulator, clock, reset, IRQ, DMA and SMMU implementations only where the kernel has those facilities.
- Verify the exact WCN3990 firmware boot/authentication protocol and remote-processor owner. Do not assume the WCN6750 WPSS flow or WCN6750 firmware layout applies.
- Implement and test actual transport I/O beneath the existing QRTR/QMI codecs. Callback fixtures are test adapters only.
- Add real service-specific QMI schemas and message tables from compatible WCN3990 sources. Generic QMI framing alone is insufficient.
- Add bounded, auditable logs for each bring-up step, including resource values and failure stage, without exposing Wi-Fi credentials or per-device secrets.
- Add an explicit stop/quiesce routine and failure-injection tests for every acquired resource.
- Keep network interface registration disabled until firmware-ready, WLFW initialization, CE/data-path readiness and a real MAC source are all verified.

## Other driver/firmware porting lessons

Apply the same shared-resource and lifecycle patterns to other Violet devices, but keep per-device implementations separate:
- Display/DSI and panel: verify panel compatible, reset sequence, regulators, clocks, DSI lanes and backlight source.
- Touch: verify controller ID, bus, IRQ polarity/trigger, reset and coordinate transform.
- Audio/ADSP: verify remoteproc, GLINK/QRTR, firmware segments, codec/amp topology and calibration source.
- Bluetooth: verify the actual WCN3990 transport and UART/serial-engine wiring; do not infer it from Wi-Fi.
- Camera: verify every sensor ID, CSI lane map, clock, regulator, EEPROM/calibration and ISP dependency.
- Sensors: verify I2C/SPI addresses, IRQ and identity registers.
- USB/charging/PMIC: verify controller and PMIC revision, PHY, charger and battery-gauge bindings.
- GNSS/modem: keep modem, EFS, NV and calibration access isolated; no synthetic identity or calibration.

## Test results captured during this audit

- Existing repository CTest baseline in `build`: **14/14 passed**.
- `python scripts/wifi/run_hal_tests.py`: **passed**:
  - `test_wifi_hal: ALL OK`
  - `test_qrtr_qmi: ALL OK`
  - `test_firmware_boot: ALL OK`
  - `test_wifi_firmware: real firmware validated`
  - `test_wifi_firmware: ALL OK`

These are host-side test results. They do not establish physical WCN3990 power-up, firmware boot, WLFW service discovery, WLAN scan, association, DHCP or packet transmission.

## Repository safety note

At audit time the working tree already had user changes in `build-native/`, `docs/platforms/VIOLET_WIFI_BRINGUP.md`, and `platform/violet/host_services.c`. Those files were not modified by this audit. Preserve them and review their diffs before any later cleanup.

No partition write, firmware flash, EDL operation, or device mutation was performed.

## AArch64 build verification

After the audit document was added, `python scripts/build_kernel_aarch64.py` completed successfully and linked `build/kernel-aarch64/vqeaf-kernel-aarch64.elf` (446,056 bytes). This confirms the current AArch64 source set, including the WCN3990 transport/firmware modules listed by the build script, compiles and links. It is still a build-only result, not a physical-radio test.
