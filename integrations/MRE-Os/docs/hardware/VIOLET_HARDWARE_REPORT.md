# Violet Hardware Report

Date: 2026-11-23
Project: MRE OS
Target: Xiaomi Redmi Note 7 Pro (violet)

## Executive status

The project scaffold, initial MRE HAL contracts, host driver registry, firmware policy interface and Violet platform YAML have been created. The host build and initial unit tests pass. Four requested upstream repositories have been cloned at pinned commits and the three device trees have been checked out for inspection. No hardware driver is yet certified.

## Confirmed reference facts

- Target SoC family: Qualcomm SM6150 / Snapdragon 675.
- CPU family: Kryo 460, 8 cores, AArch64.
- GPU family: Adreno 612.
- Device documentation lists LPDDR4X 4/6 GB, eMMC 5.1 64/128 GB, 1080 × 2340 display and nominal 4000 mAh battery.
- Xiaomi BoardConfig selects qcwcn and qca_cld3 for its Android Wi-Fi integration.
- Kernel source includes SM6150 generic DTS, KGSL/Adreno, CNSS2/ICNSS, WCNSS remoteproc, SDHCI MSM, DSI/SDE, QUPv3, USB and Qualcomm clock/power sources.

## Important conflicts / unknowns

- The generic SM6150 SoC DTS includes an embedded UFS alias; Violet device documentation identifies eMMC 5.1. The exact board DTB must decide the actual storage controller. The generic DTS must not be copied wholesale.
- No Violet-specific source DTS was found in the inspected kernel tree. One device tree includes prebuilt DTBO and Image.gz-dtb, but the binaries are not treated as source.
- BoardConfig variants differ in kernel source path and ARM architecture variant.
- Panel ID, touch controller, PMIC, physical memory map, secure carveouts, GIC ownership, UART and timer setup remain unverified.
- Proprietary manifests contain extensive vendor binaries and firmware references; none have been copied into MRE OS.

## Architecture decision

MRE OS remains an independent AArch64 operating system. Linux SM6150 kernel and Android device trees are reference inputs only. Android Framework, ART, Dalvik, Android userspace and Linux kernel runtime dependencies are prohibited.

## Current test evidence

- CMake host configuration: PASS (MinGW GCC 16.1.0).
- Driver manager and firmware policy unit tests: PASS (1/1 CTest target).
- ARM64 kernel build: NOT RUN; no AArch64 bare-metal compiler has been confirmed.
- Boot test / hardware test: NOT RUN.

## Next milestone

Obtain the exact Violet DTB/DTBO from a read-only device dump or legally usable source, parse it into the MRE hardware manifest, then define the AArch64 boot ABI and kernel entry contract. Do not enable unverified drivers or flash partitions.

## Kernel Core update — 2026-11-23

- Host CTest: 3/3 passed.
- AArch64 compile: 12 C/assembly sources passed with Clang target aarch64-none-elf.
- AArch64 link: build-aarch64/mre_kernel.elf (ELF64/AArch64), entry 0x81000, vectors 0x81800, no relocations, no undefined symbols.
- MMU tables now use 1 GiB/2 MiB block descriptors and split to 4 KiB pages for text protection and reserved-memory holes. Host tests cover a 4 GiB map and W^X.
- GICv3, Generic Timer and Qualcomm GENI UART implementations compile. UART FIFO is covered by a mocked-bus host test; hardware IRQ and MMIO behavior remain untested.
- The available reference DTB root is qcom,sm6150-idp, not xiaomi,violet. It has a zero-sized RAM reg and 7 dynamic reserved nodes. The MRE board identity and MMU gates correctly reject it.
- A package attempt using that IDP DTB was intentionally refused. No Image.gz-dtb or boot.img was created.
- ADB and fastboot are installed but no device is connected. No fastboot boot was attempted and no partition was written.
