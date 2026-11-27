# QEAF ICNSS Native Backend — implementation report (2026-11-27)

> **Architecture classification (important):** this is a QEAF-native **research prototype** in the retained freestanding AArch64/QEMU track, plus host tests. It is **not** the production Violet Linux 4.14.215 driver integration. The production path remains `platform/violet/` on vendor Linux 4.14.215, where Linux owns ICNSS, IRQ, DMA, clocks, regulators and WLAN driver binding. Do not use the freestanding ELF or QEMU results as phone boot/WLAN evidence.


## Summary

The Violet ICNSS/WCN3990 path now has a stronger Device Tree resource parser, a GIC IRQ lifecycle API, a checked Phase-2 callback contract, a WLFW session orchestrator, and an advanced Wi-Fi settings/policy page. The implementation is compiled for AArch64 and host-tested. **It is not yet a complete physical SM6150 backend**: the PMIC/RPMh, clock, WLAN power/PD, SMMU/IOMMU and coherent DMA providers are not implemented/registered. Consequently, the driver refuses to enter Phase-2 and will not power on WLAN.

## Implemented

### ICNSS resource discovery

- Parse the actual Violet `qcom,icnss` node and require `status=okay` (or absent status), three exact `reg-names` entries, the SNOC register window, SMMU IOVA/IPA windows, MSA fixed region, regulator phandles, IOMMU stream ID and SMMU S1-bypass property.
- Treat `reg[0]` as SNOC MMIO only; `reg[1]`/`reg[2]` are address apertures, never MMIO.
- Resolve inherited root `interrupt-parent` through the SM6150 PDC domain to GICv3.
- Convert the 12 DT SPI numbers 414–425 to GIC INTIDs 446–457 and retain trigger/type cells.
- Fail resource validation if GIC hierarchy, CE interrupt count, SMMU windows or MSA range are incomplete.
- The boot probe performs additive ICNSS DT discovery and maps only the SNOC window as Device memory before MMU enable. It does not enable WLAN power or start WLAN-PD.

### IRQ, DMA and Phase-2

- Add GIC `disable_irq` and `unregister_handler`; unregister first disables the IRQ, waits for distributor completion and then clears the handler under a short interrupt mask.
- Add Violet IRQ request/release through the initialized GIC bridge.
- Bump the WCN3990 Phase-2 callback ABI to v5. IRQ teardown returns a status; a failed IRQ release prevents freeing MSA/power/DMA resources while an interrupt may still be in flight.
- Add `vqeaf_violet_hw_ops_t` for real PMIC regulator, clock, WLAN power, MSA, coherent DMA, CE IRQ and time/delay providers.
- Implement callback adapters, SNOC MMIO bounds checks, CE setup/disable/RRI operations and 37-bit DMA address validation.
- Crucially, `vqeaf_violet_bind_phase2()` returns `-ENOSYS` until a complete board-verified provider is registered. Host tests verify that no power/IRQ/DMA setup is attempted without it.

### WLFW lifecycle

- Add `wlfw_session.c` orchestration for QRTR/QMI connect, MSA info/ready, capability request, BDF, optional REGDB, calibration report/download, factory MAC and final WLFW_FW_READY indication.
- Preflight refuses missing calibration or an invalid/missing factory MAC. It does not invent MAC/calibration values and does not write persist/NV.
- The session expects a real WLFW service endpoint and authenticated per-device inputs. The current smoke test deliberately returns a timeout to prove the client fails closed.

### Advanced Wi-Fi settings

- Add an advanced settings page under Wi-Fi with backend status and firmware/calibration staging paths.
- CNSS2 PCIe policy requires a PCIe transport, a live PCI enumeration and a registered native CNSS2 driver. Violet's discovered WLAN transport is ICNSS/SNOC, so CNSS2 is refused there; no CNSS2 driver is included in this build.
- Firmware/calibration replacement is not enabled. The UI only records staging paths. The policy helper only checks caller-supplied `sha256_verified`/`signature_verified` and identity flags; it does **not** calculate SHA-256 or perform signature verification itself. A trusted verifier, board identity provider, authenticated calibration-backup provider and atomic staging/install service are not connected. The UI therefore keeps installation locked and only saves staging paths; no firmware or calibration is changed.

## Build and test results

| Check | Result | Scope |
|---|---:|---|
| Violet ICNSS DT parser with `violet-reference.dtb` | PASS | Host; real DTB fixture |
| SPI→GIC INTID mapping and PDC parent resolution | PASS | Host parser test |
| Phase-2 teardown and absent-provider gate | PASS | Host mock |
| Advanced backend/package policy | PASS | Host |
| WLFW session preflight/timeout | PASS | Host/mock transport |
| Wi-Fi/WCN3990 CTest selection | 9/9 PASS | Host |
| WLFW session CTest | 1/1 PASS | Host |
| Violet Wi-Fi bring-up Python contracts | 15/15 PASS | Host |
| QRTR/QMI Python tests | 4/4 PASS | Host |
| WLFW client Python tests | 2/2 PASS | Host |
| `scripts/build_violet.py` | PASS | AArch64-musl PID 1 + drives archive |
| `scripts/build_kernel_aarch64.py` | PASS | AArch64 freestanding ELF |
| `scripts/test_kernel_aarch64.py` | PASS | QEMU `virt` emulation only |

Artifacts:

- `build/violet/vqeaf-init` — 12,785,504 bytes (295 objects)
- `build/kernel-aarch64/vqeaf-kernel-aarch64.elf` — 829,856 bytes
- `build/violet/libvqeaf-drives.a` — 303,614 bytes (17 objects)

The QEMU test proves the independent AArch64 `virt` kernel's entry, EL1, MMU, exception vectors, GICv2, timer, EL0 tasks, IPC, compositor and virtio storage. It does **not** exercise the SM6150 GICv3/PDC, PM6150 regulators, WCN3990 CE, ICNSS, WLFW or phone display.

## Device boot and physical test status

- Fastboot reports product `violet` (serial `ca1af25d`).
- No `fastboot boot` was issued in this work.
- The standalone ELF is linked at `0x40000000` by `kernel/arch/aarch64/kernel.ld`, which explicitly targets QEMU `virt`; its entry code uses QEMU PL011/ramfb/virtio. It is **not** a Violet Fastboot boot image. Passing this ELF to the phone would be an invalid test.
- The Linux vendor source tree under WSL is experimental 4.14.117, not the exact 4.14.215 device kernel. A valid device image must be built from the matching Violet kernel/boot ABI and validated separately before RAM boot.
- No WLAN/Modem PD was started on the phone. Previous live traces show that WLAN-PD attempts can reset the device; WLFW `0x45` was not advertised and the exact device's persist calibration/MAC backup is still missing.

## Required work before a physical WLAN attempt

1. Implement PM6150/RPMh regulator operations for the four DT supplies, clock/XO sequencing, WLAN reset/remoteproc/PD lifecycle and reverse-order rollback using the exact SM6150 binding and kernel source.
2. Implement the matching apps-SMMU context/stream mapping for WCN3990 and a cache-coherent 37-bit DMA allocator; prove MSA and CE rings are visible to the WCN bus master.
3. Verify the complete CE interrupt mapping/acknowledgement against PDC/GICv3 on Violet, including teardown and spurious/error interrupts.
4. Obtain/read-only verify the original device's calibration and factory MAC backup; never synthesize either.
5. Confirm modem/WLAN-PD stability and a rescue/serial capture path before powering WLAN.
6. Build an actual Violet-compatible kernel + boot image, validate its DTB/boot header/hash, then perform RAM boot only. After boot logs confirm no regression, enable WLAN in a separate controlled test.
7. Connect a trusted signature verifier and atomic QEAF firmware staging service before unlocking replacement of firmware/calibration.

**No partition writes, erase, format, p74 persist writes or p77 recovery writes were made.**


## Clarifications added after final source audit

- The `build/kernel-aarch64/vqeaf-kernel-aarch64.elf` produced by `scripts/build_kernel_aarch64.py` is the retained **QEMU `virt` reference kernel**. `kernel/arch/aarch64/kernel.ld` links it at `0x40000000`; its entry and device drivers use the QEMU PL011/ramfb/virtio environment. It is not an Android/Violet boot image and must never be passed to `fastboot boot`.
- The ICNSS code linked into that reference kernel is not called by the QEMU boot scenario and does not receive physical SM6150 MMIO, PMIC, SMMU or WLAN interrupts. Host tests validate parser/policy/state-machine contracts only.
- The FDT test converts the recorded GIC SPI cells to INTID values for diagnostic checks. This does not prove that those values can be used as Linux virtual IRQ numbers or that PDC/GICv3 hardware dispatch is correct. Production Linux must obtain IRQs through its irqdomain APIs and the exact board DT.
- `vqeaf_wifi_firmware_package_validate()` is a policy gate around verification claims; it is not a cryptographic verifier. A real trusted signature/hash implementation must populate its evidence only after verification.
- Production integration must be implemented/reviewed against the exact Violet Linux 4.14.215 source/config/DT. The currently inspected WSL tree is experimental 4.14.117 and is not a production base.
