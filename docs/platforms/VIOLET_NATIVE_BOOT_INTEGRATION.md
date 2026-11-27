# Violet native boot integration — MRE → QEAFIVELS

Date: 2026-11-23; reference-track label updated 2026-11-25

This document describes the retained freestanding AArch64 reference kernel.
It is not the production Violet Linux boot path. Linux 4.14 is the approved
kernel core; production hardware belongs to Linux drivers and QEAF services.

## Integrated modules

The MRE early-boot components are now compiled into the QEAF AArch64 kernel through an explicit platform bridge:

- `kernel/platform/aarch64/violet_boot_probe.c`: FDT parse, Violet identity/resource discovery, retained immutable report.
- `kernel/platform/aarch64/violet_bootstrap.c`: ordered native bootstrap API.
- `kernel/platform/aarch64/violet_memory.c`: EFI-style PMM descriptors, excluding DTB reservations, reserved-memory, kernel image and DTB.
- `kernel/platform/aarch64/violet_irq_bridge.c`: MRE GIC + Generic Timer to QEAF IRQ scheduler adapter.
- `kernel/include/vqeaf/violet_boot.h`: QEAF-facing contract.
- MRE FDT, Violet discovery, MMU, GICv2/v3 and Generic Timer sources are part of `scripts/build_kernel_aarch64.py`.

## API sequence

A future Violet-specific entry point must supply the DTB pointer/size, physical DTB address and linker-derived kernel/text/rodata ranges, then call:

1. `vqeaf_violet_native_bootstrap(...)`
2. The bootstrap validates FDT and root board identity, checks reserved-memory completeness, builds the PMM map, builds MRE page tables and enables the MMU.
3. Only after successful MMU enable and verified EL2/EL3 GIC system-register access may the caller invoke `vqeaf_violet_irq_init()`.
4. The IRQ vector enters the Violet GIC dispatcher only after that explicit initialization. Timer interrupts are reflected into the existing QEAF tick/scheduler path.

The MMU page-table attributes were aligned with QEAF's existing `vm.c` convention: AttrIdx0 = normal write-back, AttrIdx1 = device-nGnRE. This permits the existing QEAF per-task VM code to add user mappings on the MRE-built kernel root. QEMU continues using its existing MAIR and GICv2 path.

Entering the Violet DTB probe mode disables QEMU-only GICv2 fallback for that boot, including when the supplied DTB is invalid or does not verify as Violet.

## Safety gates

- FDT discovery does not access MMIO.
- PMM initialization refuses unverified board identity, missing verified boot handoff, incomplete memory map, dynamic/unplaced reservations, and RAM above the current 4 GiB QEAF PMM limit. It does not truncate higher RAM.
- A `LIVE_HANDSET` DTB source flag is discovery evidence only. `vqeaf_violet_native_bootstrap()` requires the explicit `BOOT_HANDOFF` source flag before PMM/MMU activation; the real Violet entry point must derive that flag only from a validated boot protocol, not from a caller/UI option.
- MMU enable refuses to proceed unless PMM and page-table construction succeeded; the MRE MMU routine also refuses if SCTLR_EL1.M/C are already enabled.
- GIC and timer are not initialized by discovery or by the bootstrap function. They remain an explicit later operation.
- The MRE GICv3 path still requires a verified ICC_SRE access path through the actual secure-firmware/EL2 handoff. That has not been established on the handset.

## SM6150 display/DSI and GENI UART (2026-11-25)

The independent AArch64 build now contains a bounded SM6150 DSI 6G-v2.3.1
controller core. It detects the shifted version, programs the current XML
RGB888 video timing and four-lane mapping, performs the 20 ms controller reset,
and waits for a real video-done or error interrupt with a poll limit. The
NT36672A Shenchao reference mode is explicitly burst mode with non-continuous
clocking; it remains reference data, not proof of the fitted panel.

The display lifecycle still fails closed. READY requires an authenticated boot
handoff, enabled MMU, complete discovered nodes/supplies/lanes, explicit panel
timing and verified command data, working clock/regulator/GPIO providers,
verified panel initialization/deinitialization, DSI video-done, verified DPU
scanout, and backlight. The DSI controller core does not implement the
panel-dependent 14 nm PHY/PLL trim, DISPCC/DPU/SMMU providers, or the actual
NT36672A command arrays.

The GENI UART backend remains explicit and fail-closed. Successful attachment
still requires verified clocks, pins, inherited UART configuration and MMU
mapping. Polled TX timeouts now stop the command engine with bounded
cancel-then-abort recovery before detaching the backend.

Host verification: 28/28 native CTest tests passed, including fake-MMIO DSI and
GENI recovery tests; `scripts/build_kernel_aarch64.py` linked the AArch64 ELF;
`scripts/test_kernel_aarch64.py` passed the QEMU regression. These are host and
emulated results, not physical display or UART evidence.

## Host evidence

The current repository reference DTB parsed successfully with these values:

- board identity verified: false
- GIC: v3 discovered
- GENI UART: discovered
- Generic Timer: discovered
- memory regions: 0
- fixed reservations: 20
- dynamic/unplaced reservations: 7
- MMU prerequisite gate: unsupported

This fixture is the SoC reference DTS, not the live Redmi Note 7 Pro Violet board DTB. The negative test confirms the native bootstrap stops at board identity before PMM/MMU/GIC/timer activation.

- CTest: 17/17 passed on the MinGW host build after rebuilding all targets from source.
- AArch64 ELF: linked successfully; no undefined symbols and no relocations.
- The AArch64 ELF still uses the existing QEMU linker script and entry address `0x40000000`. It is not a Violet boot image and is not suitable for flashing.

## Remaining blockers before device boot

1. Acquire the live Violet DTB (or exact matching stock Violet boot image + DTBO).
2. Verify root compatible, real RAM map, all reserved-memory placements and DTB physical range.
3. Determine actual bootloader handoff EL, MMU/cache state, DTB register and kernel load address.
4. Produce a dedicated Violet linker script and entry path. The current AArch64 `start.S` and `kernel.ld` are still QEMU-virt oriented.
5. Expand the PMM beyond 4 GiB if the live memory map requires it; test the 64-table MMU pool against actual carveouts.
6. Verify ICC_SRE_EL2/EL1 access, redistributor affinity and timer PPI routing on hardware.
7. Obtain exact GPT/partition metadata, matching stock firmware and a verified recovery route before any partition-write proposal.

No bootloader, GPT or phone partition has been modified. No firmware image has been flashed.
