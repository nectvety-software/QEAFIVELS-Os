# QEAF ICNSS Native Backend - continuation prompt (2026-11-27)

Act as the senior Linux kernel/Qualcomm WLAN integration engineer for `D:\QEAFIVELS-Os`.

## Mission
Continue the Violet WCN3990/ICNSS work safely. The goal is a real production Wi-Fi path on Redmi Note 7 Pro (`violet`, SM6150), but the current work is **PARTIAL / HARDWARE BLOCKED**. Do not claim that Wi-Fi works.

## Read first

1. `.agents/AGENTS.md`
2. `.agents/prompts/QEAFIVELS_OS_MASTER_PROMPT.md`
3. `.agents/skills/violet-port/SKILL.md` and `.agents/skills/device-drivers/SKILL.md`
4. `docs/platforms/VIOLET_WIFI_BRINGUP.md`
5. `docs/wifi/ICNSS_NATIVE_BACKEND_PHASE_2026-11-27.md`
6. `docs/wifi/ICNSS_TO_QEAF_PORT_MAP.md`

## Architecture and provenance rules

- Production Violet path is `platform/violet/` on vendor Linux `4.14.215-azure-Pixel+`. Linux owns ICNSS/WCN3990 device probe/remove, IRQ domains, DMA/SMMU, clocks, regulators, reset and power/PD lifecycle. QEAF userspace may expose capability-aware services and UI, but must not perform privileged MMIO or emulate the kernel driver in PID 1.
- The source checkout `/root/src/micode-violet-q-oss-exp` is experimental kernel 4.14.117, not the exact production 4.14.215 source. Do not use it as a drop-in base for a production patch. First locate and verify the matching source, config, board DTS/DTBO and build toolchain.
- `kernel/drivers/wifi/wcn3990/violet_platform.c`, Phase-2 ABI v5, GIC bridge, `wlfw_session.c` and `wifi_advanced.c` are a freestanding/reference prototype with host tests. Keep it separate from the production Linux kernel. The `vqeaf-kernel-aarch64.elf` is a QEMU `virt` ELF at `0x40000000` (PL011/ramfb/virtio), not a phone boot image.

## Current evidence (2026-11-27)

- Wi-Fi/WCN CTest: 9/9 PASS; WLFW session smoke: 1/1 PASS.
- Violet Wi-Fi bring-up Python: 15/15 PASS; QRTR/QMI: 4/4 PASS; WLFW client: 2/2 PASS.
- `scripts/build_violet.py` succeeds; `scripts/build_kernel_aarch64.py` and `scripts/test_kernel_aarch64.py` succeed only on QEMU `virt`.
- Fastboot detected `ca1af25d`, product `violet`. This phase issued no `fastboot boot`, flash, erase or partition write.
- Existing device evidence: WLFW QMI service `0x45` not published, `wlan0` absent, and WLAN-PD attempts can reset the modem/device. Per-unit calibration/factory MAC backup is still missing. Treat these as last-observed blockers and re-check read-only before any new action.

## Required engineering sequence

1. **Source audit:** verify the exact 4.14.215 ICNSS/WCN3990 lineage, compatible QMI/WLFW v01 definitions, Violet board DTS and `.config`. Record commits/hashes and license notices. REA may assist with read-only artifact inventory; it is not a driver converter and no firmware reverse-engineering result may be claimed without a real analysis artifact/log.
2. **DT/resource contract:** reconcile ICNSS `reg`/`reg-names`, IOMMU stream ID/context, MSA reserved memory, CE interrupt specifiers, PDC parent and regulator/clock supplies against the exact target DT. Never interpret Linux virtual IRQ numbers as GIC INTIDs. Use Linux irqdomain APIs in production.
3. **Power sequencing:** implement/review the real PM6150/RPMh regulators (`vdd-cx-mx`, `vdd-1.8-xo`, `vdd-1.3-rfa`, `vdd-3.3-ch0`), XO/clock dependencies, reset, WLAN-PD/remoteproc/PIL ownership, timeouts, SSR, reverse rollback and watchdog/recovery handling. Do not guess voltage values or startup delays; derive them from source/DT/PMIC binding.
4. **DMA/SMMU/CE:** use the correct Linux DMA-coherent API and real apps-SMMU context/stream configuration. Verify address width, cache coherency, ring alignment, CE register layout, interrupt acknowledgement/error paths, teardown and recovery. Never pass a raw physical address to WLAN without proving the IOMMU mapping.
5. **WLFW:** require a genuinely discovered QRTR server `0x45`; implement the matching MSA/CAP/BDF/REGDB/CAL/MAC/FW_READY exchange with the exact firmware schema. Do not fabricate service announcements, bypass QRTR/QMI, generate a MAC, substitute generic BDF or create calibration data.
6. **Per-device data:** acquire and verify a read-only backup of this handset's calibration/factory MAC from its actual persist/EFS layout. Keep p74 read-only; never write p74 during bring-up. Do not replace calibration based only on generic firmware files.
7. **Advanced settings:** keep CNSS2 disabled on Violet's integrated ICNSS/SNOC path. CNSS2 may be selected only on another target after PCIe enumeration and a native CNSS2 driver are proven. Firmware/calibration settings remain staging-only until a real trusted SHA-256/signature/board-ID verifier and atomic installer/rollback are connected. Boolean policy flags are not cryptographic verification.
8. **Validation:** run unit/host tests, production kernel compile, DTB validation, image structure/hash audit and negative failure/rollback tests. Keep QEMU results separate from phone results.
9. **Device gate:** first establish rescue/serial logs and modem reset recovery. Then build a proper Violet Linux 4.14.215 boot image and use `fastboot boot` (RAM only) after explicit authorization and compatibility validation. Never send the QEMU ELF to the phone. Only after kernel logs show stable ICNSS/WLFW should a separate WLAN enable test begin.

## Forbidden actions unless separately and explicitly authorized

- No `fastboot flash`, `erase`, `format`, partition writes, bootloader unlock or radio firmware writes.
- No writes to p74/persist, p76/boot or p77/recovery.
- No WLAN/modem PD start while reset/recovery/log capture is unproven.
- No CNSS2 forcing on Violet, fake `wlan0`, fake MAC, fake BDF/calibration, fake WLFW service or success markers.

## Acceptance criteria

Wi-Fi remains `BLOCKED_FIRMWARE`/`UNSUPPORTED` until all real stages pass: exact source/config/DT; stable power/PD; real QRTR WLFW server; MSA and firmware ready; correct per-device BDF/calibration/MAC; real `wlan0`; RF scan; WPA association; IP/DHCP; suspend/resume and SSR recovery. Report the exact evidence for each stage.

## Completion report

Update the dated report and prompt with exact files changed, source provenance, build commands, test counts, image hashes, physical tests actually run, remaining blockers and the next smallest verifiable milestone. Distinguish host, QEMU, RAM-boot and physical WLAN evidence.
