# Boot Chain Analysis — Violet

Status: BLOCKED pending inspection of the exact handset bootloader chain and boot image handoff ABI.

## Source evidence

The Xiaomi Violet BoardConfig declares boot header version 1, kernel image name Image.gz-dtb, 4096-byte kernel pages, kernel base 0x00000000, a separate DTBO image and inclusion of DTB in boot image. It also contains an Android-specific early console argument referencing msm_geni_serial at 0x880000 and ttyMSM0.

These are Android build declarations from a device tree, not proof that a new MRE image can safely use the same handoff. The third device tree contains prebuilt dtbo.img and Image.gz-dtb, but these binaries are not used as MRE boot artifacts.

## Required evidence

1. Exact device variant and bootloader version.
2. Boot image header/version accepted by the installed bootloader.
3. Kernel entry contract, execution state, exception level and CPU state.
4. DTB location, DTBO application order and handoff convention.
5. Reserved memory, framebuffer, modem and secure-world carveouts.
6. GIC state, timer frequency, UART route and console availability.
7. Secure boot / AVB constraints and recovery path.

## Planned stages

- Stage 0: preserve stock boot chain; use read-only inspection and offline artifacts.
- Stage 1: create a minimal AArch64 image with exception vectors and serial diagnostics only after ABI confirmation.
- Stage 2: establish MMU, memory allocator, interrupt controller and timer.
- Stage 3: parse the supplied DTB and discover devices.
- Stage 4: add essential drivers behind MRE HAL.

No bootloader is replaced and no phone partition is written by the current project.

## 2026-11-23 AArch64 image header inspection

The supplied reference Image.gz-dtb was decompressed (gzip stream only) and its ARM64 Image header inspected:

- text_offset: 0x80000
- image_size: 0x2e75000
- flags: 0xA
- magic: ARM64 Image magic at header offset 0x38

The local Violet BoardConfig declares boot header version 1, 4096-byte pages, kernel image name Image.gz-dtb, DTB included in the boot image and a separate DTBO image. The BoardConfig base field is 0; this does not by itself establish the physical DRAM base used at runtime.

The MRE ELF is linked at offset 0x81000 (0x80000 Image text offset plus a 0x1000 header-to-entry branch offset). It has no ELF relocations and no undefined symbols. This is a link-time offset for a position-independent ARM64 payload, not a claim about the physical address selected by the bootloader.

## Current boot gate

The available prebuilt FDT root identifies qcom,sm6150-idp, not the Violet handset. Its RAM reg is zero-sized and seven reserved-memory nodes have no fixed reg. The MRE package script refuses this tree, and the MMU prerequisite check refuses to prepare page tables.

ADB and fastboot are installed on the build workstation, but no handset is connected. No boot.img was packaged, no fastboot boot was run, and no partition was written.

A verified live Violet DTB (or exact stock Violet boot.img plus matching DTBO) is required before creating an Image.gz-dtb/boot.img candidate. Temporary testing must use fastboot boot only after the product and unlocked state are confirmed. No flash command is part of the workflow.
