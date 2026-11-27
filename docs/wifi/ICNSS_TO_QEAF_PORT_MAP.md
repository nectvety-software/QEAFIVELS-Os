# Violet ICNSS/QMI: Android kernel source to QEAF port map

## Scope and verdict

The imported six-file ICNSS/WLFW source set is Linux 4.14 kernel driver code, not a standalone Android HAL. The converter classified it as `REQUIRES_PORT`: Linux driver-model and logging APIs need replacement. It is not safe to run a mass rename over this source.

QEAF already has an independent native WCN3990 implementation. The correct conversion strategy is protocol/behavior extraction and reconciliation with that implementation, not embedding the Linux ICNSS module or Android services.

| Imported source responsibility | QEAF-native counterpart | Port status |
|---|---|---|
| ICNSS platform probe/remove, resources and power sequencing (`icnss.c`) | `kernel/drivers/wifi/wcn3990/violet_platform.c`; `VqeafWcn3990P2Ops` contract in `kernel/include/vqeaf/wcn3990_phase2.h` | Partial; exact DT/regulator/IRQ/DMA binding and rollback need device evidence |
| QRTR endpoint and QMI framing | `qrtr_transport.c`; `qmi_service.c` | Native implementation exists; compare packet framing, endpoint lifecycle and timeout/error behavior |
| WLFW v01 requests/indications | `wlfw_client.c`; `kernel/include/vqeaf/wlfw_client.h` | Native client exists; verify every message ID/TLV and target firmware version |
| Firmware download and MSA lifecycle | `firmware_boot.c`; `wifi_firmware.c`; `firmware_boot.h` | State machine exists; firmware/BDF/calibration matching and MSA address remain device-gated |
| Copy Engine configuration and DMA rings | `wcn3990_ce.c`; `wcn3990_phase2.h` | Native CE code exists; physical DMA/IOMMU/IRQ binding is not validated by host tests |
| Linux logging, workqueues, PM, device model | QEAF diagnostics, callbacks and service lifecycle | Must be rewritten; do not map semantics by macro substitution |

## Conversion result

- Imported references: `build/android-driver-input/violet-icnss-core/`.
- Converter output: `build/qeaf-driver-convert/violet-icnss-core/`.
- `port-plan.json`: six source files inventoried with SHA-256.
- `reference_source/`: imported source preserved unchanged.
- `qeaf_driver_skeleton.c`: fail-closed skeleton returns `-2` until a real QEAF probe is implemented.
- No Android init, Binder, HIDL, HAL or Linux kernel module was copied into the QEAF runtime.

## Validation

- Converter unit tests: 2/2 PASS.
- Native CTest selection (`wcn3990|wifi|violet_drivers`): 6/6 PASS.
- These are host regression tests only. They do not prove WLFW service `0x45`, firmware boot, `wlan0`, RF scan, association, DHCP or modem stability on Violet.

## Remaining gates before a real-device conversion can be accepted

1. Confirm the exact Violet SM6150 kernel/DT revision and ICNSS resource contract.
2. Verify the WLFW v01 schema against the matching firmware and firmware version; do not infer from a different Qualcomm SoC.
3. Obtain the exact device's valid persist calibration/MAC data; never synthesize or write it during conversion.
4. Establish a non-resetting modem/WLAN-PD recovery path and serial/rescue logs before power-on tests.
5. Implement and test the missing hardware callbacks, then test RAM-boot only. Do not flash p76 or touch p77/p74 for this work.

## Implementation update (2026-11-27)

The current port map now has a tested **reference prototype**, with a strict boundary to the production path:

| Area | New prototype result | Production/device status |
|---|---|---|
| FDT resource discovery | Reads the real `violet-reference.dtb`; validates 3 `reg-names`, SNOC vs IOVA/IPA windows, MSA, regulator/IOMMU declarations and inherited interrupt parent | Host-tested only; must be reconciled with exact 4.14.215 board DTS |
| CE IRQ | Parser reports 12 SPI cells 414-425 as diagnostic GIC INTIDs 446-457; trigger/type cells retained | PDC/GICv3 physical delivery and Linux virtual IRQ mapping not tested |
| IRQ lifecycle | GIC disable/unregister API and checked Phase-2 free-IRQ callback added in the freestanding reference tree | Not a substitute for Linux `request_irq()`/irqdomain integration |
| Power/clock | Callback contract and rollback boundary added | PM6150/RPMh regulator, XO/clock, reset and WLAN-PD providers NOT IMPLEMENTED |
| DMA/SMMU | Provider contract, 37-bit DMA range checks and CE adapter added | No coherent allocator or WCN SMMU context actually registered; WLAN start blocked |
| WLFW | Session sequencing and preflight; real-transport timeout test | No physical WLFW `0x45` endpoint observed; no device session run |
| Advanced settings | CNSS2 PCIe policy and firmware/calibration staging path UI | CNSS2 driver absent; firmware install intentionally locked |

### Evidence and safe continuation

- Host tests: Wi-Fi/WCN CTest 9/9 PASS, separate WLFW session CTest 1/1 PASS, Violet Wi-Fi contract tests 15/15 PASS, QRTR/QMI tests 4/4 PASS and WLFW client tests 2/2 PASS.
- AArch64 `build_violet.py` produced the userspace PID 1; `build_kernel_aarch64.py` and `test_kernel_aarch64.py` passed only for the separate QEMU `virt` reference kernel.
- Device `ca1af25d` was detected in Fastboot with product `violet`; this task did not issue `fastboot boot`, flash, erase or partition writes.
- Do not start WLAN/modem PD until the PMIC/clock/SMMU/DMA path, recovery/log capture and per-unit calibration/factory MAC backup are proven. Prior device attempts have caused resets.
- Do not enable CNSS2 on Violet. CNSS2 is available only for a platform where PCIe WLAN is actually enumerated and a native CNSS2 driver is present.
- Do not describe the settings path fields or policy flags as a firmware installer or cryptographic verification. The trusted verifier and atomic installer are still absent.

Next production milestone: port/complete the resource, power, IRQ, DMA and WLFW behavior in the exact Linux 4.14.215 ICNSS/WCN3990 driver path, using Linux regulator/clock/remoteproc/irqdomain/DMA APIs. Keep the freestanding prototype as a reference/test aid only.
