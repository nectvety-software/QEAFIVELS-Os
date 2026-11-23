# AArch64 Kernel Core — Boot and Validation Report

Date: 2026-11-23
Target: Redmi Note 7 Pro (Violet), Qualcomm SM6150
Project: MRE OS

## Implemented

- AArch64 entry assembly with IRQ masking, EL2-to-EL1 transition, counter access enablement, GIC system-register access setup, BSS clearing, stack setup and VBAR_EL1 installation.
- 2 KiB-aligned exception vector table and C exception-frame dispatcher.
- FDT v17 parser for node/property traversal, compatible matching, reg translation and 64-bit memory reservation map.
- MMU 4-level translation tables with 4 KiB pages, 2 MiB and 1 GiB blocks, block splitting for fine-grained protection, identity-mapped RAM/MMIO, W^X and reserved-range exclusion.
- MMU enable routine with SCTLR_EL1 state checks, MAIR, TCR, PARange-derived IPS, TTBR0, TLB invalidation and WXN.
- GICv2/v3 core, including GICv3 redistributor affinity selection and SGI/PPI frame offsets.
- ARM Generic Physical Timer with GIC handler registration and periodic reload.
- Qualcomm GENI UART TX with explicit inherited-clock/pin-state gates, FIFO watermark handling and newline conversion.
- Violet FDT discovery and a debugger-readable boot report.

## Build/test results

- Host CTest: 3/3 passed.
- AArch64 cross-compile: 12 source files passed.
- AArch64 link: PASS, ELF64 machine AArch64.
- ELF entry: 0x81000 (link-time image offset, not a verified physical RAM address).
- Vector table: 0x81800.
- No ELF relocations and no undefined symbols.
- MMU host tests map a 4 GiB RAM window using block descriptors and split only the region requiring page-level permissions.

## Critical source audit

The available Image.gz-dtb in device_xiaomi_violet_redminote7pro/prebuilt is not a confirmed Violet board DTB. Its root compatible list is:

- qcom,sm6150-idp
- qcom,sm6150
- qcom,idp

Its dtbo.img has one overlay entry (ID 0). The supplied DTB reports:

- GICv3 Distributor: 0x17A00000
- GICv3 Redistributor: 0x17A60000
- Qualcomm GENI console: 0x00880000
- UART SPI number 0x259 (GIC INTID 633)
- Non-secure physical timer PPI number 2 (GIC INTID 18)
- Memory node reg is zero-sized
- 20 fixed reserved-memory regions and 7 dynamic reserved nodes

These addresses are SoC reference candidates only. The generic IDP tree is not accepted as a Violet board description. MRE marks board_identity_verified=0 and refuses MMU preparation.

## Boot-image layout

The reference kernel ARM64 Image header reports text_offset=0x80000 and flags=0xA; the device BoardConfig declares boot header version 1, 4 KiB pages, Image.gz-dtb, and a separate DTBO image.

The ELF is linked at offset 0x81000, with no relocations. This is an internal link-time offset for the candidate ARM64 Image layout. It is not a claim that the physical RAM base or bootloader-selected load address has been verified.

tools/package_violet_kernel.py refuses to package the generic IDP DTB. A package requires a live/confirmed Violet DTB with xiaomi,violet root identity, a non-zero runtime RAM map, and resolved reserved-memory ranges.

## Device boot test

ADB and fastboot are installed, but both report no connected devices. No boot image was created and no boot test was run.

tools/fastboot_boot_test.ps1 is prepared for a future temporary RAM boot. It checks for exactly one device, product violet, and an unlocked bootloader, then runs fastboot boot. It contains no flash, erase, or partition-write command.

## Next required input

Connect the actual Violet handset and provide a read-only live DTB dump (or an exact stock Violet boot image plus its matching DTBO). Do not substitute the generic SM6150 IDP DTB. After that, reconcile the memory map, carveouts, bootloader handoff and image header before attempting fastboot boot.
